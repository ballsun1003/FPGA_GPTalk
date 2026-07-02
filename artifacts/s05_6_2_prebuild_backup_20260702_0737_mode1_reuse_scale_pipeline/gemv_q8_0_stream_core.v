`timescale 1ns / 1ps

// Q8_0 fixed-scale GEMV stream core.
//
// Input packet order per row group and Q8_0 block:
//   1. LANES signed int32 scale_q words, one per lane.
//   2. Q8_BLOCK_SIZE * LANES signed int8 weights.
//      Weight bytes are packed little-endian into each stream beat:
//      byte0=tdata[7:0], byte1=tdata[15:8], ...
//      TDATA_WIDTH=32 feeds 4 lanes/beat; TDATA_WIDTH=128 feeds 16 lanes/beat.
//
// Output mode:
//   mode=0: emit one signed int32 row output per valid row.
//           block_acc_i32 * scale_q is rounded away from zero by default,
//           shifted by scale_shift, accumulated in ROW_ACC_WIDTH, then
//           saturated to signed int32 on output.
//   mode=1: emit signed int32 block_acc_i32 per valid row and block.
//
// No FP datapath is used. GGUF Q8_0 fp16 scales must be converted to
// signed int32 scale_q by the PC layout stage.

module gemv_q8_0_stream_core #(
    parameter integer LANES = 16,
    parameter integer Q8_BLOCK_SIZE = 32,
    parameter integer TDATA_WIDTH = 32,
    parameter integer INPUT_ADDR_WIDTH = 16,
    parameter integer FEATURE_WIDTH = 32,
    parameter integer SCALE_SHIFT_WIDTH = 6,
    parameter integer SCALE_SHIFT_DEFAULT = 20,
    parameter integer ROW_ACC_WIDTH = 64,
    parameter integer ROUND_ENABLE = 1
) (
    input wire clk,
    input wire reset_p,

    input wire start,
    input wire mode,
    input wire [SCALE_SHIFT_WIDTH-1:0] scale_shift,
    input wire [FEATURE_WIDTH-1:0] in_features,
    input wire [FEATURE_WIDTH-1:0] out_features,

    output reg input_rd_en,
    output reg [INPUT_ADDR_WIDTH-1:0] input_rd_addr,
    input wire signed [15:0] input_rd_data,

    input wire [TDATA_WIDTH-1:0] s_axis_tdata,
    input wire [(TDATA_WIDTH/8)-1:0] s_axis_tkeep,
    input wire s_axis_tvalid,
    output reg s_axis_tready,
    input wire s_axis_tlast,

    output reg signed [31:0] m_axis_tdata,
    output reg m_axis_tvalid,
    input wire m_axis_tready,
    output reg m_axis_tlast,
    output reg [FEATURE_WIDTH-1:0] m_axis_row,
    output reg [FEATURE_WIDTH-1:0] m_axis_block,
    output reg [15:0] m_axis_lane,

    output reg busy,
    output reg done,
    output reg error,
    output reg [7:0] error_code,

    output reg [FEATURE_WIDTH-1:0] debug_row,
    output reg [FEATURE_WIDTH-1:0] debug_block,
    output reg [15:0] debug_lane,
    output reg signed [31:0] debug_out0,
    output reg signed [31:0] debug_out1,
    output reg signed [31:0] debug_out2,
    output reg [31:0] debug_in_count,
    output reg [31:0] debug_tlast_count,
    output reg [31:0] debug_tlast_tdata,
    output reg [(TDATA_WIDTH/8)-1:0] debug_tlast_tkeep,
    output reg signed [31:0] debug_scale0,
    output reg signed [31:0] debug_scale1,
    output reg signed [31:0] debug_scale2,
    output reg signed [31:0] debug_block0,
    output reg signed [31:0] debug_block1,
    output reg signed [31:0] debug_block2,
    output reg [31:0] debug_product0_lo,
    output reg [31:0] debug_product0_hi,
    output reg [31:0] debug_product1_lo,
    output reg [31:0] debug_product1_hi,
    output reg [31:0] debug_product2_lo,
    output reg [31:0] debug_product2_hi,
    output reg signed [31:0] debug_scaled0,
    output reg signed [31:0] debug_scaled1,
    output reg signed [31:0] debug_scaled2,
    output reg signed [31:0] debug_row_acc0,
    output reg signed [31:0] debug_row_acc1,
    output reg signed [31:0] debug_row_acc2
);

    localparam integer TKEEP_WIDTH = TDATA_WIDTH / 8;
    localparam integer WEIGHTS_PER_WORD = TDATA_WIDTH / 8;
    localparam integer SCALES_PER_WORD = TDATA_WIDTH / 32;
    localparam [FEATURE_WIDTH-1:0] FEATURE_ONE = {{(FEATURE_WIDTH-1){1'b0}}, 1'b1};
    localparam [FEATURE_WIDTH-1:0] LANES_FEATURE = LANES;
    localparam [15:0] WEIGHTS_PER_WORD_U16 = WEIGHTS_PER_WORD;
    localparam [15:0] SCALES_PER_WORD_U16 = SCALES_PER_WORD;
    localparam [15:0] LANES_U16 = LANES;

    localparam [4:0] ST_IDLE        = 5'd0;
    localparam [4:0] ST_SCALE       = 5'd1;
    localparam [4:0] ST_WEIGHT_RECV = 5'd2;
    localparam [4:0] ST_WEIGHT_APPLY= 5'd3;
    localparam [4:0] ST_INPUT_WAIT  = 5'd8;
    localparam [4:0] ST_BLOCK_DONE  = 5'd4;
    localparam [4:0] ST_AFTER_SCALE = 5'd5;
    localparam [4:0] ST_EMIT_ROW    = 5'd6;
    localparam [4:0] ST_EMIT_BLOCK  = 5'd7;
    localparam [4:0] ST_SCALE_MUL   = 5'd9;
    localparam [4:0] ST_SCALE_SHIFT = 5'd10;
    localparam [4:0] ST_SCALE_ACCUM = 5'd11;
    localparam [4:0] ST_INPUT_WAIT2 = 5'd12;
    localparam [4:0] ST_INPUT_WAIT3 = 5'd13;
    localparam [4:0] ST_INPUT_CAPTURE = 5'd14;
    localparam [4:0] ST_SCALE_SAT   = 5'd15;
    localparam [4:0] ST_SCALE_LOAD  = 5'd16;
    localparam [4:0] ST_EMIT_BLOCK_LOAD = 5'd17;

    localparam [7:0] ERR_NONE       = 8'd0;
    localparam [7:0] ERR_CONFIG     = 8'd1;
    localparam [7:0] ERR_TLAST      = 8'd2;
    localparam [7:0] ERR_BUSY_START = 8'd3;
    localparam [7:0] ERR_TKEEP      = 8'd4;

    reg [4:0] state;
    reg mode_reg;
    reg [SCALE_SHIFT_WIDTH-1:0] scale_shift_reg;
    reg [FEATURE_WIDTH-1:0] in_features_reg;
    reg [FEATURE_WIDTH-1:0] out_features_reg;
    reg [FEATURE_WIDTH-1:0] blocks_per_row_reg;
    reg [FEATURE_WIDTH-1:0] row_group_base;
    reg [FEATURE_WIDTH-1:0] block_index;

    reg [15:0] scale_lane;
    reg [15:0] weight_col;
    reg [15:0] weight_lane_base;
    reg [15:0] apply_lane_base;
    reg [15:0] emit_lane;
    reg [15:0] valid_lanes_reg;
    reg [TDATA_WIDTH-1:0] weight_word_reg;
    reg signed [15:0] input_sample_reg;

    reg signed [31:0] scale_q [0:LANES-1];
    reg signed [31:0] block_acc [0:LANES-1];
    reg signed [ROW_ACC_WIDTH-1:0] row_acc [0:LANES-1];
    reg signed [31:0] row_out [0:LANES-1];
    reg signed [31:0] mode1_emit_data_reg;
    reg signed [31:0] scale_block_acc_reg;
    reg signed [31:0] scale_q_reg;
    reg signed [63:0] scaled_product_scalar;
    reg signed [63:0] scaled_shifted_scalar;
    reg signed [ROW_ACC_WIDTH-1:0] row_acc_after_reg;

    integer i;
    integer b;

    wire last_group = (row_group_base + LANES_FEATURE >= out_features_reg);
    wire last_block = (block_index + FEATURE_ONE >= blocks_per_row_reg);
    wire [FEATURE_WIDTH-1:0] emit_lane_feature =
        {{(FEATURE_WIDTH-16){1'b0}}, emit_lane};
    wire [15:0] emit_lane_next = emit_lane + 16'd1;
    wire emit_last_lane_in_group = emit_lane_next >= valid_lanes_reg;
    wire [FEATURE_WIDTH-1:0] next_row_group_base = row_group_base + LANES_FEATURE;
    wire [FEATURE_WIDTH-1:0] next_group_remaining =
        out_features_reg - next_row_group_base;
    wire [15:0] start_valid_lanes =
        (out_features < LANES_FEATURE) ? out_features[15:0] : LANES_U16;
    wire [15:0] next_valid_lanes =
        (next_group_remaining < LANES_FEATURE) ? next_group_remaining[15:0] : LANES_U16;
    wire last_weight_word_in_block =
        (weight_col == (Q8_BLOCK_SIZE - 1)) &&
        (weight_lane_base + WEIGHTS_PER_WORD >= LANES);
    wire final_input_word = last_group && last_block && last_weight_word_in_block;
    wire s_axis_fire = s_axis_tvalid && s_axis_tready;
    wire last_scale_word_in_block = (scale_lane + SCALES_PER_WORD >= LANES);
    wire [TKEEP_WIDTH-1:0] expected_tkeep = {TKEEP_WIDTH{1'b1}};
    wire s_axis_tkeep_ok = (s_axis_tkeep == expected_tkeep);
    wire [127:0] s_axis_tdata_ext =
        (TDATA_WIDTH == 128) ? s_axis_tdata : {96'd0, s_axis_tdata[31:0]};
    wire [127:0] weight_word_ext =
        (TDATA_WIDTH == 128) ? weight_word_reg : {96'd0, weight_word_reg[31:0]};
    wire signed [63:0] scale_product_now =
        mul_i32_i32_to_i64(scale_block_acc_reg, scale_q_reg);
    wire signed [63:0] scale_shifted_now =
        round_shift_i64(scaled_product_scalar, scale_shift_reg);
    wire signed [ROW_ACC_WIDTH-1:0] row_acc_after_scalar =
        row_acc[emit_lane] + scaled_shifted_scalar;
    wire row_acc_after_fits_i32 =
        row_acc_after_scalar[ROW_ACC_WIDTH-1:31] == {(ROW_ACC_WIDTH-31){row_acc_after_scalar[31]}};
    wire signed [31:0] row_out_after_scalar =
        row_acc_after_fits_i32 ? row_acc_after_scalar[31:0] :
        (row_acc_after_scalar[ROW_ACC_WIDTH-1] ? 32'sh80000000 : 32'sh7fffffff);
    wire row_acc_after_reg_fits_i32 =
        row_acc_after_reg[ROW_ACC_WIDTH-1:31] == {(ROW_ACC_WIDTH-31){row_acc_after_reg[31]}};
    wire signed [31:0] row_out_after_reg =
        row_acc_after_reg_fits_i32 ? row_acc_after_reg[31:0] :
        (row_acc_after_reg[ROW_ACC_WIDTH-1] ? 32'sh80000000 : 32'sh7fffffff);

    function signed [7:0] get_weight_byte;
        input [127:0] word;
        input integer byte_index;
        begin
            case (byte_index)
                0: get_weight_byte = word[7:0];
                1: get_weight_byte = word[15:8];
                2: get_weight_byte = word[23:16];
                3: get_weight_byte = word[31:24];
                4: get_weight_byte = word[39:32];
                5: get_weight_byte = word[47:40];
                6: get_weight_byte = word[55:48];
                7: get_weight_byte = word[63:56];
                8: get_weight_byte = word[71:64];
                9: get_weight_byte = word[79:72];
                10: get_weight_byte = word[87:80];
                11: get_weight_byte = word[95:88];
                12: get_weight_byte = word[103:96];
                13: get_weight_byte = word[111:104];
                14: get_weight_byte = word[119:112];
                15: get_weight_byte = word[127:120];
                default: get_weight_byte = 8'sd0;
            endcase
        end
    endfunction

    function signed [31:0] get_scale_word;
        input [127:0] word;
        input integer scale_index;
        begin
            case (scale_index)
                0: get_scale_word = word[31:0];
                1: get_scale_word = word[63:32];
                2: get_scale_word = word[95:64];
                3: get_scale_word = word[127:96];
                default: get_scale_word = 32'sd0;
            endcase
        end
    endfunction

    function signed [31:0] mul_i16_i8_to_i32;
        input signed [15:0] x;
        input signed [7:0] w;
        reg signed [31:0] x32;
        reg signed [31:0] w32;
        begin
            x32 = {{16{x[15]}}, x};
            w32 = {{24{w[7]}}, w};
            mul_i16_i8_to_i32 = x32 * w32;
        end
    endfunction

    function signed [63:0] mul_i32_i32_to_i64;
        input signed [31:0] a;
        input signed [31:0] b;
        reg signed [63:0] a64;
        reg signed [63:0] b64;
        begin
            a64 = {{32{a[31]}}, a};
            b64 = {{32{b[31]}}, b};
            mul_i32_i32_to_i64 = a64 * b64;
        end
    endfunction

    function signed [63:0] round_shift_i64;
        input signed [63:0] value;
        input [SCALE_SHIFT_WIDTH-1:0] shift;
        reg signed [63:0] rounding;
        reg signed [63:0] abs_value;
        begin
            if (shift == {SCALE_SHIFT_WIDTH{1'b0}}) begin
                round_shift_i64 = value;
            end else if (ROUND_ENABLE != 0) begin
                rounding = 64'sd1 <<< (shift - 1'b1);
                if (value >= 64'sd0) begin
                    round_shift_i64 = (value + rounding) >>> shift;
                end else begin
                    abs_value = -value;
                    round_shift_i64 = -((abs_value + rounding) >>> shift);
                end
            end else begin
                round_shift_i64 = value >>> shift;
            end
        end
    endfunction

    function signed [31:0] saturate_to_i32;
        input signed [ROW_ACC_WIDTH-1:0] value;
        begin
            if (value[ROW_ACC_WIDTH-1:31] == {(ROW_ACC_WIDTH-31){value[31]}}) begin
                saturate_to_i32 = value[31:0];
            end else if (value[ROW_ACC_WIDTH-1]) begin
                saturate_to_i32 = 32'sh80000000;
            end else begin
                saturate_to_i32 = 32'sh7fffffff;
            end
        end
    endfunction


    always @(posedge clk) begin
        if (reset_p) begin
            state <= ST_IDLE;
            mode_reg <= 1'b0;
            scale_shift_reg <= SCALE_SHIFT_DEFAULT;
            in_features_reg <= {FEATURE_WIDTH{1'b0}};
            out_features_reg <= {FEATURE_WIDTH{1'b0}};
            blocks_per_row_reg <= {FEATURE_WIDTH{1'b0}};
            row_group_base <= {FEATURE_WIDTH{1'b0}};
            block_index <= {FEATURE_WIDTH{1'b0}};
            scale_lane <= 16'd0;
            weight_col <= 16'd0;
            weight_lane_base <= 16'd0;
            apply_lane_base <= 16'd0;
            emit_lane <= 16'd0;
            valid_lanes_reg <= 16'd0;
            weight_word_reg <= {TDATA_WIDTH{1'b0}};
            input_sample_reg <= 16'sd0;
            input_rd_en <= 1'b0;
            input_rd_addr <= {INPUT_ADDR_WIDTH{1'b0}};
            s_axis_tready <= 1'b0;
            m_axis_tdata <= 32'sd0;
            m_axis_tvalid <= 1'b0;
            m_axis_tlast <= 1'b0;
            m_axis_row <= {FEATURE_WIDTH{1'b0}};
            m_axis_block <= {FEATURE_WIDTH{1'b0}};
            m_axis_lane <= 16'd0;
            busy <= 1'b0;
            done <= 1'b0;
            error <= 1'b0;
            error_code <= ERR_NONE;
            debug_row <= {FEATURE_WIDTH{1'b0}};
            debug_block <= {FEATURE_WIDTH{1'b0}};
            debug_lane <= 16'd0;
            debug_out0 <= 32'sd0;
            debug_out1 <= 32'sd0;
            debug_out2 <= 32'sd0;
            debug_in_count <= 32'd0;
            debug_tlast_count <= 32'd0;
            debug_tlast_tdata <= 32'd0;
            debug_tlast_tkeep <= {(TDATA_WIDTH/8){1'b0}};
            debug_scale0 <= 32'sd0;
            debug_scale1 <= 32'sd0;
            debug_scale2 <= 32'sd0;
            debug_block0 <= 32'sd0;
            debug_block1 <= 32'sd0;
            debug_block2 <= 32'sd0;
            debug_product0_lo <= 32'd0;
            debug_product0_hi <= 32'd0;
            debug_product1_lo <= 32'd0;
            debug_product1_hi <= 32'd0;
            debug_product2_lo <= 32'd0;
            debug_product2_hi <= 32'd0;
            debug_scaled0 <= 32'sd0;
            debug_scaled1 <= 32'sd0;
            debug_scaled2 <= 32'sd0;
            debug_row_acc0 <= 32'sd0;
            debug_row_acc1 <= 32'sd0;
            debug_row_acc2 <= 32'sd0;
            for (i = 0; i < LANES; i = i + 1) begin
                scale_q[i] <= 32'sd0;
                block_acc[i] <= 32'sd0;
                row_acc[i] <= {ROW_ACC_WIDTH{1'b0}};
                row_out[i] <= 32'sd0;
            end
            scale_block_acc_reg <= 32'sd0;
            mode1_emit_data_reg <= 32'sd0;
            scale_q_reg <= 32'sd0;
            scaled_product_scalar <= 64'sd0;
            scaled_shifted_scalar <= 64'sd0;
            row_acc_after_reg <= {ROW_ACC_WIDTH{1'b0}};
        end else begin
            done <= 1'b0;
            input_rd_en <= 1'b0;
            s_axis_tready <= 1'b0;
            case (state)
                ST_IDLE: begin
                    busy <= 1'b0;
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast <= 1'b0;
                    if (start) begin
                        error <= 1'b0;
                        error_code <= ERR_NONE;
                        if (busy) begin
                            error <= 1'b1;
                            error_code <= ERR_BUSY_START;
                        end else if (
                            in_features == {FEATURE_WIDTH{1'b0}} ||
                            out_features == {FEATURE_WIDTH{1'b0}} ||
                            (in_features % Q8_BLOCK_SIZE) != 0 ||
                            (TDATA_WIDTH != 32 && TDATA_WIDTH != 128) ||
                            (TDATA_WIDTH % 32) != 0 ||
                            (LANES % WEIGHTS_PER_WORD) != 0 ||
                            (LANES % SCALES_PER_WORD) != 0 ||
                            scale_shift >= 6'd63
                        ) begin
                            error <= 1'b1;
                            error_code <= ERR_CONFIG;
                        end else begin
                            busy <= 1'b1;
                            mode_reg <= mode;
                            scale_shift_reg <= scale_shift;
                            in_features_reg <= in_features;
                            out_features_reg <= out_features;
                            blocks_per_row_reg <= in_features / Q8_BLOCK_SIZE;
                            row_group_base <= {FEATURE_WIDTH{1'b0}};
                            block_index <= {FEATURE_WIDTH{1'b0}};
                            scale_lane <= 16'd0;
                            weight_col <= 16'd0;
                            weight_lane_base <= 16'd0;
                            input_sample_reg <= 16'sd0;
                            emit_lane <= 16'd0;
                            valid_lanes_reg <= start_valid_lanes;
                            debug_row <= {FEATURE_WIDTH{1'b0}};
                            debug_block <= {FEATURE_WIDTH{1'b0}};
                            debug_lane <= 16'd0;
                            debug_out0 <= 32'sd0;
                            debug_out1 <= 32'sd0;
                            debug_out2 <= 32'sd0;
                            debug_in_count <= 32'd0;
                            debug_tlast_count <= 32'd0;
                            debug_tlast_tdata <= 32'd0;
                            debug_tlast_tkeep <= {(TDATA_WIDTH/8){1'b0}};
                            debug_scale0 <= 32'sd0;
                            debug_scale1 <= 32'sd0;
                            debug_scale2 <= 32'sd0;
                            debug_block0 <= 32'sd0;
                            debug_block1 <= 32'sd0;
                            debug_block2 <= 32'sd0;
                            debug_product0_lo <= 32'd0;
                            debug_product0_hi <= 32'd0;
                            debug_product1_lo <= 32'd0;
                            debug_product1_hi <= 32'd0;
                            debug_product2_lo <= 32'd0;
                            debug_product2_hi <= 32'd0;
                            debug_scaled0 <= 32'sd0;
                            debug_scaled1 <= 32'sd0;
                            debug_scaled2 <= 32'sd0;
                            debug_row_acc0 <= 32'sd0;
                            debug_row_acc1 <= 32'sd0;
                            debug_row_acc2 <= 32'sd0;
                            for (i = 0; i < LANES; i = i + 1) begin
                                scale_q[i] <= 32'sd0;
                                block_acc[i] <= 32'sd0;
                                row_acc[i] <= {ROW_ACC_WIDTH{1'b0}};
                                row_out[i] <= 32'sd0;
                            end
                            state <= ST_SCALE;
                        end
                    end
                end

                ST_SCALE: begin
                    s_axis_tready <= 1'b1;
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= scale_lane;
                    if (s_axis_fire) begin
                        if (s_axis_tlast) begin
                            debug_tlast_count <= debug_in_count;
                            debug_tlast_tdata <= s_axis_tdata;
                            debug_tlast_tkeep <= s_axis_tkeep;
                        end
                        debug_in_count <= debug_in_count + 32'd1;
                        if (!s_axis_tkeep_ok) begin
                            debug_tlast_count <= debug_in_count;
                            debug_tlast_tdata <= s_axis_tdata;
                            debug_tlast_tkeep <= s_axis_tkeep;
                            error <= 1'b1;
                            error_code <= ERR_TKEEP;
                            busy <= 1'b0;
                            s_axis_tready <= 1'b0;
                            state <= ST_IDLE;
                        end else if (s_axis_tlast) begin
                            error <= 1'b1;
                            error_code <= ERR_TLAST;
                            busy <= 1'b0;
                            s_axis_tready <= 1'b0;
                            state <= ST_IDLE;
                        end else begin
                            for (b = 0; b < SCALES_PER_WORD; b = b + 1) begin
                                if (scale_lane + b < LANES) begin
                                    scale_q[scale_lane + b] <= get_scale_word(s_axis_tdata_ext, b);
                                end
                            end
                            if (last_scale_word_in_block) begin
                                scale_lane <= 16'd0;
                                weight_col <= 16'd0;
                                weight_lane_base <= 16'd0;
                                for (i = 0; i < LANES; i = i + 1) begin
                                    block_acc[i] <= 32'sd0;
                                end
                                state <= ST_WEIGHT_RECV;
                            end else begin
                                scale_lane <= scale_lane + SCALES_PER_WORD_U16;
                            end
                        end
                    end
                end

                ST_WEIGHT_RECV: begin
                    s_axis_tready <= 1'b1;
                    input_rd_en <= 1'b1;
                    input_rd_addr <= (block_index * Q8_BLOCK_SIZE) + weight_col;
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= weight_lane_base;
                    if (s_axis_fire) begin
                        s_axis_tready <= 1'b0;
                        if (s_axis_tlast) begin
                            debug_tlast_count <= debug_in_count;
                            debug_tlast_tdata <= s_axis_tdata;
                            debug_tlast_tkeep <= s_axis_tkeep;
                        end
                        debug_in_count <= debug_in_count + 32'd1;
                        if (!s_axis_tkeep_ok) begin
                            debug_tlast_count <= debug_in_count;
                            debug_tlast_tdata <= s_axis_tdata;
                            debug_tlast_tkeep <= s_axis_tkeep;
                            error <= 1'b1;
                            error_code <= ERR_TKEEP;
                            busy <= 1'b0;
                            state <= ST_IDLE;
                        end else if (s_axis_tlast != final_input_word) begin
                            error <= 1'b1;
                            error_code <= ERR_TLAST;
                            busy <= 1'b0;
                            state <= ST_IDLE;
                        end else begin
                            weight_word_reg <= s_axis_tdata;
                            apply_lane_base <= weight_lane_base;
                            state <= ST_INPUT_WAIT;
                        end
                    end
                end


                ST_INPUT_WAIT: begin
                    input_rd_en <= 1'b1;
                    input_rd_addr <= (block_index * Q8_BLOCK_SIZE) + weight_col;
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= apply_lane_base;
                    state <= ST_INPUT_WAIT2;
                end

                ST_INPUT_WAIT2: begin
                    input_rd_en <= 1'b1;
                    input_rd_addr <= (block_index * Q8_BLOCK_SIZE) + weight_col;
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= apply_lane_base;
                    state <= ST_INPUT_WAIT3;
                end

                ST_INPUT_WAIT3: begin
                    input_rd_en <= 1'b1;
                    input_rd_addr <= (block_index * Q8_BLOCK_SIZE) + weight_col;
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= apply_lane_base;
                    state <= ST_INPUT_CAPTURE;
                end

                ST_INPUT_CAPTURE: begin
                    input_rd_en <= 1'b1;
                    input_rd_addr <= (block_index * Q8_BLOCK_SIZE) + weight_col;
                    input_sample_reg <= input_rd_data;
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= apply_lane_base;
                    state <= ST_WEIGHT_APPLY;
                end

                ST_WEIGHT_APPLY: begin
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= apply_lane_base;
                    if (TDATA_WIDTH == 128) begin
                        for (b = 0; b < LANES; b = b + 1) begin
                            block_acc[b] <=
                                block_acc[b] +
                                mul_i16_i8_to_i32(input_sample_reg, get_weight_byte(weight_word_ext, b));
                        end
                    end else begin
                        for (b = 0; b < WEIGHTS_PER_WORD; b = b + 1) begin
                            if (apply_lane_base + b < LANES) begin
                                    block_acc[apply_lane_base + b] <=
                                    block_acc[apply_lane_base + b] +
                                    mul_i16_i8_to_i32(input_sample_reg, get_weight_byte(weight_word_ext, b));
                            end
                        end
                    end

                    if (last_weight_word_in_block) begin
                        weight_col <= 16'd0;
                        weight_lane_base <= 16'd0;
                        state <= ST_BLOCK_DONE;
                    end else if (weight_lane_base + WEIGHTS_PER_WORD >= LANES) begin
                        weight_lane_base <= 16'd0;
                        weight_col <= weight_col + 16'd1;
                        state <= ST_WEIGHT_RECV;
                    end else begin
                        weight_lane_base <= weight_lane_base + WEIGHTS_PER_WORD_U16;
                        state <= ST_WEIGHT_RECV;
                    end
                end

                ST_BLOCK_DONE: begin
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= 16'd0;
                    if (mode_reg) begin
                        emit_lane <= 16'd0;
                        m_axis_tvalid <= 1'b0;
                        state <= ST_EMIT_BLOCK_LOAD;
                    end else begin
                        emit_lane <= 16'd0;
                        state <= ST_SCALE_LOAD;
                    end
                end

                ST_SCALE_LOAD: begin
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= emit_lane;
                    scale_block_acc_reg <= block_acc[emit_lane];
                    scale_q_reg <= scale_q[emit_lane];
                    if (emit_lane == 16'd0) begin
                        debug_scale0 <= scale_q[emit_lane];
                        debug_block0 <= block_acc[emit_lane];
                    end else if (emit_lane == 16'd1) begin
                        debug_scale1 <= scale_q[emit_lane];
                        debug_block1 <= block_acc[emit_lane];
                    end else if (emit_lane == 16'd2) begin
                        debug_scale2 <= scale_q[emit_lane];
                        debug_block2 <= block_acc[emit_lane];
                    end
                    state <= ST_SCALE_MUL;
                end

                ST_SCALE_MUL: begin
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= emit_lane;
                    scaled_product_scalar <= scale_product_now;
                    if (emit_lane == 16'd0) begin
                        debug_product0_lo <= scale_product_now[31:0];
                        debug_product0_hi <= scale_product_now[63:32];
                    end else if (emit_lane == 16'd1) begin
                        debug_product1_lo <= scale_product_now[31:0];
                        debug_product1_hi <= scale_product_now[63:32];
                    end else if (emit_lane == 16'd2) begin
                        debug_product2_lo <= scale_product_now[31:0];
                        debug_product2_hi <= scale_product_now[63:32];
                    end
                    state <= ST_SCALE_SHIFT;
                end

                ST_SCALE_SHIFT: begin
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= emit_lane;
                    scaled_shifted_scalar <= scale_shifted_now;
                    if (emit_lane == 16'd0) begin
                        debug_scaled0 <= scale_shifted_now[31:0];
                    end else if (emit_lane == 16'd1) begin
                        debug_scaled1 <= scale_shifted_now[31:0];
                    end else if (emit_lane == 16'd2) begin
                        debug_scaled2 <= scale_shifted_now[31:0];
                    end
                    state <= ST_SCALE_ACCUM;
                end

                ST_SCALE_ACCUM: begin
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= emit_lane;
                    row_acc[emit_lane] <= row_acc_after_scalar;
                    row_acc_after_reg <= row_acc_after_scalar;
                    state <= ST_SCALE_SAT;
                end

                ST_SCALE_SAT: begin
                    debug_row <= row_group_base;
                    debug_block <= block_index;
                    debug_lane <= emit_lane;
                    row_out[emit_lane] <= row_out_after_reg;
                    if (emit_lane == 16'd0) begin
                        debug_row_acc0 <= row_acc_after_reg[31:0];
                    end else if (emit_lane == 16'd1) begin
                        debug_row_acc1 <= row_acc_after_reg[31:0];
                    end else if (emit_lane == 16'd2) begin
                        debug_row_acc2 <= row_acc_after_reg[31:0];
                    end
                    if (emit_lane == LANES - 1) begin
                        emit_lane <= 16'd0;
                        state <= ST_AFTER_SCALE;
                    end else begin
                        emit_lane <= emit_lane + 16'd1;
                        state <= ST_SCALE_LOAD;
                    end
                end

                ST_AFTER_SCALE: begin
                    if (last_block) begin
                        emit_lane <= 16'd0;
                        m_axis_tvalid <= 1'b0;
                        state <= ST_EMIT_ROW;
                    end else begin
                        block_index <= block_index + FEATURE_ONE;
                        scale_lane <= 16'd0;
                        weight_col <= 16'd0;
                        weight_lane_base <= 16'd0;
                        for (i = 0; i < LANES; i = i + 1) begin
                            scale_q[i] <= 32'sd0;
                            block_acc[i] <= 32'sd0;
                        end
                        state <= ST_SCALE;
                    end
                end

                ST_EMIT_ROW: begin
                    if (m_axis_tvalid) begin
                        if (m_axis_tready) begin
                            m_axis_tvalid <= 1'b0;
                            m_axis_tlast <= 1'b0;
                            if (emit_last_lane_in_group) begin
                                emit_lane <= 16'd0;
                                if (last_group) begin
                                    busy <= 1'b0;
                                    done <= 1'b1;
                                    state <= ST_IDLE;
                                end else begin
                                    row_group_base <= next_row_group_base;
                                    valid_lanes_reg <= next_valid_lanes;
                                    block_index <= {FEATURE_WIDTH{1'b0}};
                                    scale_lane <= 16'd0;
                                    weight_col <= 16'd0;
                                    weight_lane_base <= 16'd0;
                                    for (i = 0; i < LANES; i = i + 1) begin
                                        scale_q[i] <= 32'sd0;
                                        block_acc[i] <= 32'sd0;
                                        row_acc[i] <= {ROW_ACC_WIDTH{1'b0}};
                                        row_out[i] <= 32'sd0;
                                    end
                                    state <= ST_SCALE;
                                end
                            end else begin
                                emit_lane <= emit_lane + 16'd1;
                            end
                        end
                    end else begin
                        m_axis_tdata <= row_out[emit_lane];
                        m_axis_tvalid <= 1'b1;
                        m_axis_tlast <= last_group && emit_last_lane_in_group;
                        m_axis_row <= row_group_base + emit_lane_feature;
                        m_axis_block <= block_index;
                        m_axis_lane <= emit_lane;
                        if (row_group_base + emit_lane_feature == 0) begin
                            debug_out0 <= row_out[emit_lane];
                        end else if (row_group_base + emit_lane_feature == 1) begin
                            debug_out1 <= row_out[emit_lane];
                        end else if (row_group_base + emit_lane_feature == 2) begin
                            debug_out2 <= row_out[emit_lane];
                        end
                        debug_row <= row_group_base + emit_lane_feature;
                        debug_block <= block_index;
                        debug_lane <= emit_lane;
                    end
                end

                ST_EMIT_BLOCK_LOAD: begin
                    mode1_emit_data_reg <= block_acc[emit_lane];
                    debug_row <= row_group_base + emit_lane_feature;
                    debug_block <= block_index;
                    debug_lane <= emit_lane;
                    state <= ST_EMIT_BLOCK;
                end

                ST_EMIT_BLOCK: begin
                    if (m_axis_tvalid) begin
                        if (m_axis_tready) begin
                            m_axis_tvalid <= 1'b0;
                            m_axis_tlast <= 1'b0;
                            if (emit_last_lane_in_group) begin
                                emit_lane <= 16'd0;
                                if (last_block) begin
                                    if (last_group) begin
                                        busy <= 1'b0;
                                        done <= 1'b1;
                                        state <= ST_IDLE;
                                    end else begin
                                        row_group_base <= next_row_group_base;
                                        valid_lanes_reg <= next_valid_lanes;
                                        block_index <= {FEATURE_WIDTH{1'b0}};
                                        scale_lane <= 16'd0;
                                        weight_col <= 16'd0;
                                        weight_lane_base <= 16'd0;
                                        for (i = 0; i < LANES; i = i + 1) begin
                                            scale_q[i] <= 32'sd0;
                                            block_acc[i] <= 32'sd0;
                                            row_acc[i] <= {ROW_ACC_WIDTH{1'b0}};
                                            row_out[i] <= 32'sd0;
                                        end
                                        state <= ST_SCALE;
                                    end
                                end else begin
                                    block_index <= block_index + FEATURE_ONE;
                                    scale_lane <= 16'd0;
                                    weight_col <= 16'd0;
                                    weight_lane_base <= 16'd0;
                                    for (i = 0; i < LANES; i = i + 1) begin
                                        scale_q[i] <= 32'sd0;
                                        block_acc[i] <= 32'sd0;
                                    end
                                    state <= ST_SCALE;
                                end
                            end else begin
                                emit_lane <= emit_lane + 16'd1;
                                state <= ST_EMIT_BLOCK_LOAD;
                            end
                        end
                    end else begin
                        m_axis_tdata <= mode1_emit_data_reg;
                        m_axis_tvalid <= 1'b1;
                        m_axis_tlast <= last_group && last_block && emit_last_lane_in_group;
                        m_axis_row <= row_group_base + emit_lane_feature;
                        m_axis_block <= block_index;
                        m_axis_lane <= emit_lane;
                        if (row_group_base + emit_lane_feature == 0) begin
                            debug_out0 <= mode1_emit_data_reg;
                        end else if (row_group_base + emit_lane_feature == 1) begin
                            debug_out1 <= mode1_emit_data_reg;
                        end else if (row_group_base + emit_lane_feature == 2) begin
                            debug_out2 <= mode1_emit_data_reg;
                        end
                        debug_row <= row_group_base + emit_lane_feature;
                        debug_block <= block_index;
                        debug_lane <= emit_lane;
                    end
                end

                default: begin
                    error <= 1'b1;
                    error_code <= ERR_CONFIG;
                    busy <= 1'b0;
                    state <= ST_IDLE;
                end
            endcase
        end
    end

endmodule
