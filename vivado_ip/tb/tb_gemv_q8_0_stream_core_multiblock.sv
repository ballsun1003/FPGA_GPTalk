`timescale 1ns / 1ps

module tb_gemv_q8_0_stream_core_multiblock;
    localparam integer LANES = 16;
    localparam integer Q8_BLOCK_SIZE = 32;
    localparam integer SCALE_SHIFT = 20;
    localparam integer TDATA_WIDTH = 128;
    localparam integer TKEEP_WIDTH = TDATA_WIDTH / 8;
    localparam integer MAX_IN_FEATURES = 1536;
    localparam integer MAX_OUT_FEATURES = 16;
    localparam integer MAX_BLOCKS = MAX_IN_FEATURES / Q8_BLOCK_SIZE;
    localparam integer MAX_MODE1_OUTPUTS = MAX_BLOCKS * LANES;
    localparam integer MAX_PACKET_BYTES = 32768;
    localparam integer TIMEOUT_CYCLES = 800000;

    reg clk;
    reg reset_p;
    reg start;
    reg mode;
    reg [5:0] scale_shift;
    reg [31:0] in_features;
    reg [31:0] out_features;

    wire input_rd_en;
    wire [15:0] input_rd_addr;
    wire signed [15:0] input_rd_data;
    reg input_half_sel_model;
    reg [31:0] input_bram_dout_model;
    wire [15:0] input_word_base = {input_rd_addr[15:1], 1'b0};

    assign input_rd_data = input_half_sel_model ?
        $signed(input_bram_dout_model[31:16]) :
        $signed(input_bram_dout_model[15:0]);

    reg [TDATA_WIDTH-1:0] s_axis_tdata;
    reg [TKEEP_WIDTH-1:0] s_axis_tkeep;
    reg s_axis_tvalid;
    wire s_axis_tready;
    reg s_axis_tlast;

    wire signed [31:0] m_axis_tdata;
    wire m_axis_tvalid;
    reg m_axis_tready;
    wire m_axis_tlast;
    wire [31:0] m_axis_row;
    wire [31:0] m_axis_block;
    wire [15:0] m_axis_lane;

    wire busy;
    wire done;
    wire error;
    wire [7:0] error_code;
    wire [31:0] debug_row;
    wire [31:0] debug_block;
    wire [15:0] debug_lane;
    wire signed [31:0] debug_out0;
    wire signed [31:0] debug_out1;
    wire signed [31:0] debug_out2;
    wire [31:0] debug_in_count;
    wire [31:0] debug_tlast_count;
    wire [31:0] debug_tlast_tdata;
    wire [TKEEP_WIDTH-1:0] debug_tlast_tkeep;
    wire signed [31:0] debug_scale0;
    wire signed [31:0] debug_scale1;
    wire signed [31:0] debug_scale2;
    wire signed [31:0] debug_block0;
    wire signed [31:0] debug_block1;
    wire signed [31:0] debug_block2;
    wire [31:0] debug_product0_lo;
    wire [31:0] debug_product0_hi;
    wire [31:0] debug_product1_lo;
    wire [31:0] debug_product1_hi;
    wire [31:0] debug_product2_lo;
    wire [31:0] debug_product2_hi;
    wire signed [31:0] debug_scaled0;
    wire signed [31:0] debug_scaled1;
    wire signed [31:0] debug_scaled2;
    wire signed [31:0] debug_row_acc0;
    wire signed [31:0] debug_row_acc1;
    wire signed [31:0] debug_row_acc2;

    reg signed [15:0] input_mem [0:MAX_IN_FEATURES-1];
    reg [7:0] packet_mem [0:MAX_PACKET_BYTES-1];
    reg signed [31:0] expected_out [0:MAX_OUT_FEATURES-1];
    reg signed [31:0] expected_block_acc [0:MAX_MODE1_OUTPUTS-1];

    string case_name;
    string case_dir;
    string case_cfg;
    string path;
    integer in_features_i;
    integer out_features_i;
    integer packet_bytes_i;
    integer blocks_per_row_i;
    integer expected_tlast_beat_i;
    integer output_count;
    integer failure_count;
    integer input_event_count;
    integer max_input_addr_seen;
    integer active;
    integer mode1_blocks_enabled;
    integer expected_output_count;

    gemv_q8_0_stream_core #(
        .LANES(LANES),
        .Q8_BLOCK_SIZE(Q8_BLOCK_SIZE),
        .TDATA_WIDTH(TDATA_WIDTH),
        .INPUT_ADDR_WIDTH(16),
        .FEATURE_WIDTH(32),
        .SCALE_SHIFT_WIDTH(6),
        .SCALE_SHIFT_DEFAULT(SCALE_SHIFT),
        .ROW_ACC_WIDTH(64),
        .ROUND_ENABLE(1)
    ) dut (
        .clk(clk),
        .reset_p(reset_p),
        .start(start),
        .mode(mode),
        .scale_shift(scale_shift),
        .in_features(in_features),
        .out_features(out_features),
        .input_rd_en(input_rd_en),
        .input_rd_addr(input_rd_addr),
        .input_rd_data(input_rd_data),
        .s_axis_tdata(s_axis_tdata),
        .s_axis_tkeep(s_axis_tkeep),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tlast(s_axis_tlast),
        .m_axis_tdata(m_axis_tdata),
        .m_axis_tvalid(m_axis_tvalid),
        .m_axis_tready(m_axis_tready),
        .m_axis_tlast(m_axis_tlast),
        .m_axis_row(m_axis_row),
        .m_axis_block(m_axis_block),
        .m_axis_lane(m_axis_lane),
        .busy(busy),
        .done(done),
        .error(error),
        .error_code(error_code),
        .debug_row(debug_row),
        .debug_block(debug_block),
        .debug_lane(debug_lane),
        .debug_out0(debug_out0),
        .debug_out1(debug_out1),
        .debug_out2(debug_out2),
        .debug_in_count(debug_in_count),
        .debug_tlast_count(debug_tlast_count),
        .debug_tlast_tdata(debug_tlast_tdata),
        .debug_tlast_tkeep(debug_tlast_tkeep),
        .debug_scale0(debug_scale0),
        .debug_scale1(debug_scale1),
        .debug_scale2(debug_scale2),
        .debug_block0(debug_block0),
        .debug_block1(debug_block1),
        .debug_block2(debug_block2),
        .debug_product0_lo(debug_product0_lo),
        .debug_product0_hi(debug_product0_hi),
        .debug_product1_lo(debug_product1_lo),
        .debug_product1_hi(debug_product1_hi),
        .debug_product2_lo(debug_product2_lo),
        .debug_scaled0(debug_scaled0),
        .debug_scaled1(debug_scaled1),
        .debug_scaled2(debug_scaled2),
        .debug_row_acc0(debug_row_acc0),
        .debug_row_acc1(debug_row_acc1),
        .debug_row_acc2(debug_row_acc2)
    );

    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    always @(posedge clk) begin
        if (reset_p) begin
            input_half_sel_model <= 1'b0;
            input_bram_dout_model <= 32'd0;
        end else if (input_rd_en) begin
            input_event_count <= input_event_count + 1;
            if (input_rd_addr > max_input_addr_seen) begin
                max_input_addr_seen <= input_rd_addr;
            end
            if (input_rd_addr < in_features_i) begin
                input_half_sel_model <= input_rd_addr[0];
                input_bram_dout_model <= {
                    input_mem[input_word_base + 16'd1],
                    input_mem[input_word_base]
                };
            end else begin
                input_half_sel_model <= 1'b0;
                input_bram_dout_model <= 32'd0;
                failure_count <= failure_count + 1;
                $display("[FAIL] %0s input_rd_addr out of range addr=%0d limit=%0d",
                         case_name, input_rd_addr, in_features_i);
            end
        end
    end

    always @(posedge clk) begin
        if (!reset_p && active && m_axis_tvalid && m_axis_tready) begin
            check_output_word;
        end
    end

    task load_case;
        integer cfg_fd;
        integer got;
        begin
            if (!$value$plusargs("CASE_CFG=%s", case_cfg)) begin
                $fatal(1, "[FAIL] missing +CASE_CFG");
            end
            cfg_fd = $fopen(case_cfg, "r");
            if (cfg_fd == 0) begin
                $fatal(1, "[FAIL] open case cfg %0s", case_cfg);
            end
            got = $fscanf(cfg_fd, "%s\n%s\n%d\n%d\n%d\n",
                          case_name, case_dir, in_features_i, out_features_i, packet_bytes_i);
            $fclose(cfg_fd);
            if (got != 5) begin
                $fatal(1, "[FAIL] parse case cfg %0s got=%0d", case_cfg, got);
            end
            if (out_features_i != LANES) begin
                $fatal(1, "[FAIL] this TB expects OUT_FEATURES=16, got %0d", out_features_i);
            end
            if (packet_bytes_i > MAX_PACKET_BYTES) begin
                $fatal(1, "[FAIL] packet too large: %0d", packet_bytes_i);
            end
            if ((packet_bytes_i % TKEEP_WIDTH) != 0) begin
                $fatal(1, "[FAIL] packet not 128-bit aligned: %0d", packet_bytes_i);
            end
            blocks_per_row_i = in_features_i / Q8_BLOCK_SIZE;
            expected_tlast_beat_i = (packet_bytes_i / TKEEP_WIDTH) - 1;

            path = {case_dir, "/input_i16.hex"};
            $readmemh(path, input_mem);
            path = {case_dir, "/packet_axis128.hex"};
            $readmemh(path, packet_mem);
            path = {case_dir, "/output_mode0_i32.hex"};
            $readmemh(path, expected_out);
            path = {case_dir, "/block_acc_i32.hex"};
            $readmemh(path, expected_block_acc);
        end
    endtask

    task reset_dut;
        begin
            reset_p = 1'b1;
            start = 1'b0;
            mode = 1'b0;
            scale_shift = SCALE_SHIFT[5:0];
            in_features = in_features_i[31:0];
            out_features = out_features_i[31:0];
            s_axis_tdata = {TDATA_WIDTH{1'b0}};
            s_axis_tkeep = {TKEEP_WIDTH{1'b1}};
            s_axis_tvalid = 1'b0;
            s_axis_tlast = 1'b0;
            m_axis_tready = 1'b1;
            output_count = 0;
            input_event_count = 0;
            max_input_addr_seen = 0;
            active = 0;
            repeat (8) @(posedge clk);
            reset_p = 1'b0;
            repeat (4) @(posedge clk);
        end
    endtask

    task send_beat;
        input [TDATA_WIDTH-1:0] data;
        input last;
        begin
            @(posedge clk);
            s_axis_tdata <= data;
            s_axis_tkeep <= {TKEEP_WIDTH{1'b1}};
            s_axis_tlast <= last;
            s_axis_tvalid <= 1'b1;
            @(posedge clk);
            while (!s_axis_tready && !error) begin
                @(posedge clk);
            end
            s_axis_tvalid <= 1'b0;
            s_axis_tlast <= 1'b0;
            s_axis_tdata <= {TDATA_WIDTH{1'b0}};
        end
    endtask

    task drive_packet;
        integer off;
        integer b;
        integer beat_index;
        reg [TDATA_WIDTH-1:0] beat;
        reg last;
        begin
            beat_index = 0;
            for (off = 0; off < packet_bytes_i; off = off + TKEEP_WIDTH) begin
                beat = {TDATA_WIDTH{1'b0}};
                for (b = 0; b < TKEEP_WIDTH; b = b + 1) begin
                    beat[b*8 +: 8] = packet_mem[off + b];
                end
                last = (beat_index == expected_tlast_beat_i);
                send_beat(beat, last);
                beat_index = beat_index + 1;
                if (error) begin
                    off = packet_bytes_i;
                end
            end
        end
    endtask

    task check_output_word;
        reg signed [31:0] expected;
        reg expected_last;
        integer expected_row;
        integer expected_block;
        integer expected_lane;
        begin
            if (output_count >= expected_output_count) begin
                failure_count <= failure_count + 1;
                $display("[FAIL] %0s extra output row=%0d block=%0d lane=%0d data=%0d",
                         case_name, m_axis_row, m_axis_block, m_axis_lane, m_axis_tdata);
            end else begin
                if (mode1_blocks_enabled) begin
                    expected = expected_block_acc[output_count];
                    expected_last = (output_count == expected_output_count - 1);
                    expected_block = output_count / LANES;
                    expected_lane = output_count % LANES;
                    expected_row = expected_lane;
                    if (m_axis_row !== expected_row[31:0] ||
                        m_axis_block !== expected_block[31:0] ||
                        m_axis_lane !== expected_lane[15:0]) begin
                        failure_count <= failure_count + 1;
                        $display("[FAIL] %0s mode1 metadata mismatch idx=%0d row=%0d/%0d block=%0d/%0d lane=%0d/%0d",
                                 case_name, output_count,
                                 m_axis_row, expected_row,
                                 m_axis_block, expected_block,
                                 m_axis_lane, expected_lane);
                    end
                end else begin
                    expected = expected_out[output_count];
                    expected_last = (output_count == expected_output_count - 1);
                    if (m_axis_row !== output_count[31:0]) begin
                        failure_count <= failure_count + 1;
                        $display("[FAIL] %0s output row mismatch idx=%0d row=%0d expected_row=%0d",
                                 case_name, output_count, m_axis_row, output_count);
                    end
                end
                if (m_axis_tlast !== expected_last) begin
                    failure_count <= failure_count + 1;
                    $display("[FAIL] %0s TLAST mismatch idx=%0d got=%0b expected=%0b",
                             case_name, output_count, m_axis_tlast, expected_last);
                end
                if (m_axis_tdata !== expected) begin
                    failure_count <= failure_count + 1;
                    $display("[FAIL] %0s result[%0d] got=%0d expected=%0d row=%0d block=%0d lane=%0d",
                             case_name, output_count, m_axis_tdata, expected,
                             m_axis_row, m_axis_block, m_axis_lane);
                    $display("[DEBUG] dbg(row=%0d block=%0d lane=%0d out0=%0d out1=%0d out2=%0d in_count=%0d tlast_count=%0d tlast_tdata=0x%08x tlast_tkeep=0x%04x)",
                             debug_row, debug_block, debug_lane, debug_out0, debug_out1, debug_out2,
                             debug_in_count, debug_tlast_count, debug_tlast_tdata, debug_tlast_tkeep);
                    $display("[DEBUG] scale0=%0d scale1=%0d scale2=%0d block0=%0d block1=%0d block2=%0d scaled0=%0d scaled1=%0d scaled2=%0d row_acc0=%0d row_acc1=%0d row_acc2=%0d",
                             debug_scale0, debug_scale1, debug_scale2,
                             debug_block0, debug_block1, debug_block2,
                             debug_scaled0, debug_scaled1, debug_scaled2,
                             debug_row_acc0, debug_row_acc1, debug_row_acc2);
                end
            end
            output_count <= output_count + 1;
        end
    endtask

    task run_mode0;
        integer cycle;
        begin
            reset_dut;
            expected_output_count = mode1_blocks_enabled ? blocks_per_row_i * LANES : out_features_i;
            $display("[CASE] %0s dir=%0s mode=%0d in=%0d out=%0d blocks=%0d packet_bytes=%0d tlast_beat=%0d expected_outputs=%0d",
                     case_name, case_dir, mode1_blocks_enabled ? 1 : 0,
                     in_features_i, out_features_i,
                     blocks_per_row_i, packet_bytes_i, expected_tlast_beat_i,
                     expected_output_count);
            @(posedge clk);
            mode <= mode1_blocks_enabled ? 1'b1 : 1'b0;
            scale_shift <= SCALE_SHIFT[5:0];
            in_features <= in_features_i[31:0];
            out_features <= out_features_i[31:0];
            start <= 1'b1;
            @(posedge clk);
            start <= 1'b0;
            active <= 1;

            fork
                drive_packet;
            join

            cycle = 0;
            while (cycle < TIMEOUT_CYCLES && !done && !error) begin
                cycle = cycle + 1;
                @(posedge clk);
            end
            repeat (8) @(posedge clk);
            active <= 0;

            if (cycle >= TIMEOUT_CYCLES) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s timeout output_count=%0d input_events=%0d max_input_addr=%0d",
                         case_name, output_count, input_event_count, max_input_addr_seen);
            end
            if (error) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s core error_code=%0d dbg(row=%0d block=%0d lane=%0d)",
                         case_name, error_code, debug_row, debug_block, debug_lane);
            end
            if (output_count != expected_output_count) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s output_count=%0d expected=%0d",
                         case_name, output_count, expected_output_count);
            end
            if (failure_count == 0) begin
                $display("[PASS] %0s mode=%0d blocks=%0d outputs=%0d input_events=%0d max_input_addr=%0d",
                         case_name, mode1_blocks_enabled ? 1 : 0,
                         blocks_per_row_i, output_count, input_event_count, max_input_addr_seen);
            end
        end
    endtask

    initial begin
        failure_count = 0;
        mode1_blocks_enabled = 0;
        if ($test$plusargs("MODE1_BLOCKS")) begin
            mode1_blocks_enabled = 1;
        end
        load_case;
        run_mode0;
        if (failure_count == 0) begin
            $display("S05_6_1_MULTIBLOCK_CASE_PASS %0s", case_name);
            $finish;
        end else begin
            $display("S05_6_1_MULTIBLOCK_CASE_FAIL %0s failures=%0d", case_name, failure_count);
            $fatal(1, "S05.6.1 multi-block case failed");
        end
    end
endmodule
