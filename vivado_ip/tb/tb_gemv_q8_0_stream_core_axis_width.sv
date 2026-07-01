`timescale 1ns / 1ps

module tb_gemv_q8_0_stream_core_axis_width;
`ifdef AXIS_TDATA_WIDTH
    localparam integer TDATA_WIDTH = `AXIS_TDATA_WIDTH;
`else
    localparam integer TDATA_WIDTH = 32;
`endif
    localparam integer LANES = 16;
    localparam integer Q8_BLOCK_SIZE = 32;
    localparam integer SCALE_SHIFT = 20;
    localparam integer IN_FEATURES = 32;
    localparam integer OUT_FEATURES_BASE = 3;
    localparam integer BLOCKS_PER_ROW = 1;
    localparam integer TKEEP_WIDTH = TDATA_WIDTH / 8;
    localparam integer SCALES_PER_BEAT = TDATA_WIDTH / 32;
    localparam integer WEIGHTS_PER_BEAT = TDATA_WIDTH / 8;
    localparam integer SCALE_BEATS_PER_BLOCK = LANES / SCALES_PER_BEAT;
    localparam integer WEIGHT_BEATS_PER_BLOCK = Q8_BLOCK_SIZE * (LANES / WEIGHTS_PER_BEAT);
    localparam integer BEATS_PER_GROUP_BLOCK = SCALE_BEATS_PER_BLOCK + WEIGHT_BEATS_PER_BLOCK;
    localparam integer INPUT_BYTES = IN_FEATURES * 2;
    localparam integer SCALE_BYTES = LANES * 4;
    localparam integer WEIGHT_BYTES = LANES * Q8_BLOCK_SIZE;
    localparam integer OUTPUT_BYTES = OUT_FEATURES_BASE * 4;
    localparam integer TIMEOUT_CYCLES = 40000;
    localparam integer ERR_TLAST = 2;
    localparam integer ERR_TKEEP = 4;

    reg clk;
    reg reset_p;
    reg start;
    reg mode;
    reg [5:0] scale_shift;
    reg [31:0] in_features;
    reg [31:0] out_features;

    wire input_rd_en;
    wire [15:0] input_rd_addr;
    reg signed [15:0] input_rd_data;

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

    reg [7:0] input_bytes [0:INPUT_BYTES-1];
    reg [7:0] scale_bytes [0:SCALE_BYTES-1];
    reg [7:0] weight_bytes [0:WEIGHT_BYTES-1];
    reg [7:0] scaled_bytes [0:OUTPUT_BYTES-1];
    reg [7:0] block_acc_bytes [0:OUTPUT_BYTES-1];

    reg signed [15:0] input_mem [0:IN_FEATURES-1];
    reg signed [31:0] scaled_ref [0:OUT_FEATURES_BASE-1];
    reg signed [31:0] block_acc_ref [0:OUT_FEATURES_BASE-1];

    string golden_dir;
    integer failure_count;
    integer output_count;
    integer expected_outputs;
    integer current_out_features;
    integer plusargs_ok;
    reg current_mode;
    reg active;

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
        .debug_product2_hi(debug_product2_hi),
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
            input_rd_data <= 16'sd0;
        end else if (input_rd_en) begin
            if (input_rd_addr < IN_FEATURES) begin
                input_rd_data <= input_mem[input_rd_addr];
            end else begin
                input_rd_data <= 16'sd0;
                failure_count <= failure_count + 1;
                $display("[FAIL] input_rd_addr out of range: %0d", input_rd_addr);
            end
        end
    end

    always @(posedge clk) begin
        if (!reset_p && active && m_axis_tvalid && m_axis_tready) begin
            check_output_word;
        end
    end

    function signed [15:0] le_i16_input;
        input integer byte_index;
        reg [15:0] bits;
        begin
            bits = {input_bytes[byte_index + 1], input_bytes[byte_index]};
            le_i16_input = $signed(bits);
        end
    endfunction

    function signed [31:0] le_i32_from_scaled;
        input integer byte_index;
        reg [31:0] bits;
        begin
            bits = {scaled_bytes[byte_index + 3], scaled_bytes[byte_index + 2], scaled_bytes[byte_index + 1], scaled_bytes[byte_index]};
            le_i32_from_scaled = $signed(bits);
        end
    endfunction

    function signed [31:0] le_i32_from_block;
        input integer byte_index;
        reg [31:0] bits;
        begin
            bits = {block_acc_bytes[byte_index + 3], block_acc_bytes[byte_index + 2], block_acc_bytes[byte_index + 1], block_acc_bytes[byte_index]};
            le_i32_from_block = $signed(bits);
        end
    endfunction

    function [31:0] scale_for_group_lane;
        input integer group;
        input integer lane;
        begin
            if (group == 0 && lane >= 0 && lane < LANES) begin
                scale_for_group_lane = {scale_bytes[lane * 4 + 3], scale_bytes[lane * 4 + 2], scale_bytes[lane * 4 + 1], scale_bytes[lane * 4]};
            end else begin
                scale_for_group_lane = 32'd0;
            end
        end
    endfunction

    function [7:0] weight_for_group_col_lane;
        input integer group;
        input integer col;
        input integer lane;
        begin
            if (group == 0 && col >= 0 && col < Q8_BLOCK_SIZE && lane >= 0 && lane < LANES) begin
                weight_for_group_col_lane = weight_bytes[col * LANES + lane];
            end else begin
                weight_for_group_col_lane = 8'd0;
            end
        end
    endfunction

    function [TDATA_WIDTH-1:0] pack_scale_beat;
        input integer group;
        input integer scale_lane_base;
        integer i;
        begin
            pack_scale_beat = {TDATA_WIDTH{1'b0}};
            for (i = 0; i < SCALES_PER_BEAT; i = i + 1) begin
                pack_scale_beat[i * 32 +: 32] = scale_for_group_lane(group, scale_lane_base + i);
            end
        end
    endfunction

    function [TDATA_WIDTH-1:0] pack_weight_beat;
        input integer group;
        input integer col;
        input integer lane_base;
        integer i;
        begin
            pack_weight_beat = {TDATA_WIDTH{1'b0}};
            for (i = 0; i < WEIGHTS_PER_BEAT; i = i + 1) begin
                pack_weight_beat[i * 8 +: 8] = weight_for_group_col_lane(group, col, lane_base + i);
            end
        end
    endfunction

    task read_input_file;
        input string path_bits;
        output integer got;
        integer fd;
        begin
            fd = $fopen(path_bits, "rb");
            if (fd == 0) $fatal(1, "[FAIL] open %0s", path_bits);
            got = $fread(input_bytes, fd);
            $fclose(fd);
            if (got != INPUT_BYTES) $fatal(1, "[FAIL] fread %0s got=%0d expected=%0d", path_bits, got, INPUT_BYTES);
        end
    endtask

    task read_scale_file;
        input string path_bits;
        output integer got;
        integer fd;
        begin
            fd = $fopen(path_bits, "rb");
            if (fd == 0) $fatal(1, "[FAIL] open %0s", path_bits);
            got = $fread(scale_bytes, fd);
            $fclose(fd);
            if (got != SCALE_BYTES) $fatal(1, "[FAIL] fread %0s got=%0d expected=%0d", path_bits, got, SCALE_BYTES);
        end
    endtask

    task read_weight_file;
        input string path_bits;
        output integer got;
        integer fd;
        begin
            fd = $fopen(path_bits, "rb");
            if (fd == 0) $fatal(1, "[FAIL] open %0s", path_bits);
            got = $fread(weight_bytes, fd);
            $fclose(fd);
            if (got != WEIGHT_BYTES) $fatal(1, "[FAIL] fread %0s got=%0d expected=%0d", path_bits, got, WEIGHT_BYTES);
        end
    endtask

    task read_scaled_file;
        input string path_bits;
        output integer got;
        integer fd;
        begin
            fd = $fopen(path_bits, "rb");
            if (fd == 0) $fatal(1, "[FAIL] open %0s", path_bits);
            got = $fread(scaled_bytes, fd);
            $fclose(fd);
            if (got != OUTPUT_BYTES) $fatal(1, "[FAIL] fread %0s got=%0d expected=%0d", path_bits, got, OUTPUT_BYTES);
        end
    endtask

    task load_golden;
        integer got;
        integer fd;
        integer i;
        string path;
        begin
            path = {golden_dir, "/input_i16.bin"};
            read_input_file(path, got);
            path = {golden_dir, "/scale_q_i32.bin"};
            read_scale_file(path, got);
            path = {golden_dir, "/weight_q8_fpga_layout.bin"};
            read_weight_file(path, got);
            path = {golden_dir, "/output_scaled_ref_i32.bin"};
            read_scaled_file(path, got);

            path = {golden_dir, "/output_block_acc_ref_i32.bin"};
            fd = $fopen(path, "rb");
            if (fd == 0) $fatal(1, "[FAIL] open %s", path);
            got = $fread(block_acc_bytes, fd);
            $fclose(fd);
            if (got != OUTPUT_BYTES) $fatal(1, "[FAIL] fread %s got=%0d expected=%0d", path, got, OUTPUT_BYTES);

            for (i = 0; i < IN_FEATURES; i = i + 1) input_mem[i] = le_i16_input(i * 2);
            for (i = 0; i < OUT_FEATURES_BASE; i = i + 1) begin
                scaled_ref[i] = le_i32_from_scaled(i * 4);
                block_acc_ref[i] = le_i32_from_block(i * 4);
            end
        end
    endtask

    task reset_dut;
        begin
            reset_p = 1'b1;
            start = 1'b0;
            mode = 1'b0;
            scale_shift = SCALE_SHIFT[5:0];
            in_features = IN_FEATURES;
            out_features = OUT_FEATURES_BASE;
            s_axis_tdata = {TDATA_WIDTH{1'b0}};
            s_axis_tkeep = {TKEEP_WIDTH{1'b1}};
            s_axis_tvalid = 1'b0;
            s_axis_tlast = 1'b0;
            m_axis_tready = 1'b1;
            active = 1'b0;
            output_count = 0;
            expected_outputs = 0;
            repeat (5) @(posedge clk);
            reset_p = 1'b0;
            repeat (2) @(posedge clk);
        end
    endtask

    task send_beat;
        input [TDATA_WIDTH-1:0] data;
        input last;
        input [TKEEP_WIDTH-1:0] keep;
        begin
            @(posedge clk);
            s_axis_tdata <= data;
            s_axis_tkeep <= keep;
            s_axis_tlast <= last;
            s_axis_tvalid <= 1'b1;
            @(posedge clk);
            while (!s_axis_tready && !error) @(posedge clk);
            s_axis_tvalid <= 1'b0;
            s_axis_tlast <= 1'b0;
            s_axis_tkeep <= {TKEEP_WIDTH{1'b1}};
            s_axis_tdata <= {TDATA_WIDTH{1'b0}};
        end
    endtask

    task drive_packet_variant;
        input integer out_features_cfg;
        input integer tlast_beat;
        input integer omit_tlast;
        input integer extra_tlast;
        input integer wrong_tkeep_beat;
        integer groups;
        integer group;
        integer scale_lane_base;
        integer col;
        integer lane_base;
        integer beat_index;
        integer expected_last;
        reg last;
        reg [TKEEP_WIDTH-1:0] keep;
        begin
            groups = (out_features_cfg + LANES - 1) / LANES;
            expected_last = groups * BEATS_PER_GROUP_BLOCK - 1;
            beat_index = 0;
            for (group = 0; group < groups; group = group + 1) begin
                for (scale_lane_base = 0; scale_lane_base < LANES; scale_lane_base = scale_lane_base + SCALES_PER_BEAT) begin
                    last = (!omit_tlast && beat_index == tlast_beat);
                    keep = (beat_index == wrong_tkeep_beat) ? ({TKEEP_WIDTH{1'b1}} ^ {{(TKEEP_WIDTH-1){1'b0}}, 1'b1}) : {TKEEP_WIDTH{1'b1}};
                    send_beat(pack_scale_beat(group, scale_lane_base), last, keep);
                    beat_index = beat_index + 1;
                    if (error) begin
                        group = groups;
                        scale_lane_base = LANES;
                    end
                end
                for (col = 0; col < Q8_BLOCK_SIZE && !error; col = col + 1) begin
                    for (lane_base = 0; lane_base < LANES; lane_base = lane_base + WEIGHTS_PER_BEAT) begin
                        last = (!omit_tlast && beat_index == tlast_beat);
                        keep = (beat_index == wrong_tkeep_beat) ? ({TKEEP_WIDTH{1'b1}} ^ {{(TKEEP_WIDTH-1){1'b0}}, 1'b1}) : {TKEEP_WIDTH{1'b1}};
                        send_beat(pack_weight_beat(group, col, lane_base), last, keep);
                        beat_index = beat_index + 1;
                        if (error) begin
                            lane_base = LANES;
                            col = Q8_BLOCK_SIZE;
                            group = groups;
                        end
                    end
                end
            end
            if (!error && extra_tlast) begin
                send_beat({TDATA_WIDTH{1'b0}}, 1'b1, {TKEEP_WIDTH{1'b1}});
            end
        end
    endtask

    task check_output_word;
        reg signed [31:0] expected;
        integer row;
        reg expected_last;
        begin
            row = m_axis_row;
            expected_last = (output_count == expected_outputs - 1);
            if (m_axis_tlast !== expected_last) begin
                failure_count = failure_count + 1;
                $display("[FAIL] tlast mismatch output=%0d got=%0b expected=%0b", output_count, m_axis_tlast, expected_last);
            end
            if (row < 0 || row >= current_out_features) begin
                failure_count = failure_count + 1;
                $display("[FAIL] row out of range row=%0d out_features=%0d", row, current_out_features);
                expected = 32'sdx;
            end else if (row < OUT_FEATURES_BASE) begin
                expected = current_mode ? block_acc_ref[row] : scaled_ref[row];
            end else begin
                expected = 32'sd0;
            end
            if (m_axis_tdata !== expected) begin
                failure_count = failure_count + 1;
                $display("[FAIL] output mismatch mode=%0d row=%0d got=%0d expected=%0d", current_mode, row, m_axis_tdata, expected);
            end
            output_count = output_count + 1;
        end
    endtask

    task run_normal_case;
        input integer out_features_cfg;
        input mode_value;
        input [255:0] case_name;
        integer cycle;
        integer start_failures;
        integer groups;
        integer expected_tlast;
        begin
            reset_dut;
            current_mode = mode_value;
            current_out_features = out_features_cfg;
            expected_outputs = out_features_cfg;
            start_failures = failure_count;
            groups = (out_features_cfg + LANES - 1) / LANES;
            expected_tlast = groups * BEATS_PER_GROUP_BLOCK - 1;
            active = 1'b1;

            @(posedge clk);
            mode <= mode_value;
            out_features <= out_features_cfg;
            start <= 1'b1;
            @(posedge clk);
            start <= 1'b0;
            drive_packet_variant(out_features_cfg, expected_tlast, 0, 0, -1);

            cycle = 0;
            while (cycle < TIMEOUT_CYCLES && !done && !error) begin
                cycle = cycle + 1;
                @(posedge clk);
            end
            repeat (2) @(posedge clk);
            active = 1'b0;
            if (cycle >= TIMEOUT_CYCLES || error || output_count != expected_outputs) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s axis=%0d out_features=%0d mode=%0d timeout=%0b error=%0b code=%0d outputs=%0d expected=%0d",
                         case_name, TDATA_WIDTH, out_features_cfg, mode_value, cycle >= TIMEOUT_CYCLES, error, error_code, output_count, expected_outputs);
            end else if (failure_count == start_failures) begin
                $display("[PASS] %0s axis=%0d out_features=%0d mode=%0d outputs=%0d",
                         case_name, TDATA_WIDTH, out_features_cfg, mode_value, output_count);
            end
        end
    endtask

    task run_error_case;
        input [255:0] case_name;
        input integer tlast_beat;
        input integer omit_tlast;
        input integer extra_tlast;
        input integer wrong_tkeep_beat;
        input integer expected_error_code;
        integer cycle;
        integer start_failures;
        begin
            reset_dut;
            start_failures = failure_count;
            current_mode = 1'b0;
            current_out_features = OUT_FEATURES_BASE;
            @(posedge clk);
            mode <= 1'b0;
            out_features <= OUT_FEATURES_BASE;
            start <= 1'b1;
            @(posedge clk);
            start <= 1'b0;
            drive_packet_variant(OUT_FEATURES_BASE, tlast_beat, omit_tlast, extra_tlast, wrong_tkeep_beat);
            cycle = 0;
            while (cycle < TIMEOUT_CYCLES && !done && !error) begin
                cycle = cycle + 1;
                @(posedge clk);
            end
            repeat (2) @(posedge clk);
            if (cycle >= TIMEOUT_CYCLES || !error || error_code !== expected_error_code) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s axis=%0d expected_error=%0d got_error=%0b code=%0d debug(row=%0d block=%0d lane=%0d)",
                         case_name, TDATA_WIDTH, expected_error_code, error, error_code, debug_row, debug_block, debug_lane);
            end else if (failure_count == start_failures) begin
                $display("[PASS] %0s axis=%0d error_code=%0d debug(row=%0d block=%0d lane=%0d)",
                         case_name, TDATA_WIDTH, error_code, debug_row, debug_block, debug_lane);
            end
        end
    endtask

    task run_order_checks;
        reg [TDATA_WIDTH-1:0] scale0;
        reg [TDATA_WIDTH-1:0] weight0;
        integer i;
        integer start_failures;
        begin
            start_failures = failure_count;
            scale0 = pack_scale_beat(0, 0);
            for (i = 0; i < SCALES_PER_BEAT; i = i + 1) begin
                if (scale0[i * 32 +: 32] !== scale_for_group_lane(0, i)) begin
                    failure_count = failure_count + 1;
                    $display("[FAIL] scale header order axis=%0d scale_slot=%0d", TDATA_WIDTH, i);
                end
            end
            weight0 = pack_weight_beat(0, 0, 0);
            for (i = 0; i < WEIGHTS_PER_BEAT; i = i + 1) begin
                if (weight0[i * 8 +: 8] !== weight_for_group_col_lane(0, 0, i)) begin
                    failure_count = failure_count + 1;
                    $display("[FAIL] weight lane order axis=%0d lane=%0d", TDATA_WIDTH, i);
                end
            end
            if (failure_count == start_failures) begin
                $display("[PASS] byte lane order, scale header order, weight lane order axis=%0d", TDATA_WIDTH);
            end
        end
    endtask

    initial begin
        if (!(TDATA_WIDTH == 32 || TDATA_WIDTH == 128)) begin
            $fatal(1, "[FAIL] unsupported TDATA_WIDTH=%0d", TDATA_WIDTH);
        end
        golden_dir = "pycharm/golden/fake_gemv";
        plusargs_ok = $value$plusargs("GOLDEN_DIR=%s", golden_dir);
        failure_count = 0;
        output_count = 0;
        expected_outputs = 0;
        current_out_features = OUT_FEATURES_BASE;
        current_mode = 1'b0;
        active = 1'b0;
        load_golden;
        run_order_checks;

        $display("[INFO] axis=%0d scale_beats=%0d weight_beats=%0d total_beats=%0d expected_tlast=%0d full_tkeep=0x%0h",
                 TDATA_WIDTH, SCALE_BEATS_PER_BLOCK, WEIGHT_BEATS_PER_BLOCK, BEATS_PER_GROUP_BLOCK,
                 BEATS_PER_GROUP_BLOCK - 1, {TKEEP_WIDTH{1'b1}});

        run_normal_case(3, 1'b0, "fake_gemv mode=0 scaled");
        run_normal_case(3, 1'b1, "fake_gemv mode=1 block_acc");
        run_normal_case(16, 1'b0, "out_features=16 mode=0");
        run_normal_case(16, 1'b1, "out_features=16 mode=1");
        run_normal_case(17, 1'b0, "out_features=17 mode=0");
        run_normal_case(17, 1'b1, "out_features=17 mode=1");

        run_error_case("early TLAST", BEATS_PER_GROUP_BLOCK - 2, 0, 0, -1, ERR_TLAST);
        run_error_case("missing TLAST", BEATS_PER_GROUP_BLOCK - 1, 1, 0, -1, ERR_TLAST);
        run_error_case("extra beat after missing TLAST", BEATS_PER_GROUP_BLOCK, 1, 1, -1, ERR_TLAST);
        run_error_case("wrong TKEEP", BEATS_PER_GROUP_BLOCK - 1, 0, 0, 0, ERR_TKEEP);

        if (failure_count == 0) begin
            $display("[PASS] tb_gemv_q8_0_stream_core_axis_width axis=%0d completed", TDATA_WIDTH);
            $finish;
        end
        $fatal(1, "[FAIL] tb_gemv_q8_0_stream_core_axis_width axis=%0d failures=%0d", TDATA_WIDTH, failure_count);
    end
endmodule
