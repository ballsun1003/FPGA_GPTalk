`timescale 1ns / 1ps

module tb_gemv_q8_0_dma_top_multiblock;
    localparam integer LANES = 16;
    localparam integer Q8_BLOCK_SIZE = 32;
    localparam integer SCALE_SHIFT = 20;
    localparam integer TDATA_WIDTH = 128;
    localparam integer TKEEP_WIDTH = TDATA_WIDTH / 8;
    localparam integer MAX_IN_FEATURES = 1536;
    localparam integer MAX_BLOCKS = MAX_IN_FEATURES / Q8_BLOCK_SIZE;
    localparam integer MAX_MODE1_OUTPUTS = MAX_BLOCKS * LANES;
    localparam integer MAX_PACKET_BYTES = 32768;
    localparam integer MAX_PACKET_BEATS = MAX_PACKET_BYTES / TKEEP_WIDTH;
    localparam integer MAX_BRAM_WORDS = MAX_IN_FEATURES / 2;
    localparam integer TIMEOUT_CYCLES = 1000000;

    localparam [11:0] GEMV_VERSION       = 12'h000;
    localparam [11:0] GEMV_CONTROL       = 12'h004;
    localparam [11:0] GEMV_STATUS        = 12'h008;
    localparam [11:0] GEMV_ERROR_CODE    = 12'h00c;
    localparam [11:0] GEMV_MODE          = 12'h010;
    localparam [11:0] GEMV_SCALE_SHIFT   = 12'h014;
    localparam [11:0] GEMV_IN_FEATURES   = 12'h018;
    localparam [11:0] GEMV_OUT_FEATURES  = 12'h01c;
    localparam [11:0] GEMV_INPUT_BASE    = 12'h020;
    localparam [11:0] GEMV_WEIGHT_LENGTH = 12'h024;
    localparam [11:0] GEMV_RESULT_LENGTH = 12'h028;
    localparam [11:0] GEMV_START         = 12'h02c;
    localparam [11:0] GEMV_DONE          = 12'h030;
    localparam [11:0] GEMV_DEBUG_ROW     = 12'h034;
    localparam [11:0] GEMV_DEBUG_BLOCK   = 12'h038;
    localparam [11:0] GEMV_DEBUG_LANE    = 12'h03c;
    localparam [11:0] GEMV_DEBUG_IN_COUNT = 12'h04c;
    localparam [11:0] GEMV_DEBUG_TLAST_COUNT = 12'h050;
    localparam [11:0] GEMV_DEBUG_TLAST_TKEEP = 12'h058;
    localparam [11:0] GEMV_BUILD_CONFIG  = 12'h0a4;

    reg clk;
    reg resetn;

    reg [11:0] s_axi_awaddr;
    reg [2:0] s_axi_awprot;
    reg s_axi_awvalid;
    wire s_axi_awready;
    reg [31:0] s_axi_wdata;
    reg [3:0] s_axi_wstrb;
    reg s_axi_wvalid;
    wire s_axi_wready;
    wire [1:0] s_axi_bresp;
    wire s_axi_bvalid;
    reg s_axi_bready;
    reg [11:0] s_axi_araddr;
    reg [2:0] s_axi_arprot;
    reg s_axi_arvalid;
    wire s_axi_arready;
    wire [31:0] s_axi_rdata;
    wire [1:0] s_axi_rresp;
    wire s_axi_rvalid;
    reg s_axi_rready;

    reg [127:0] s_axis_tdata;
    reg [15:0] s_axis_tkeep;
    reg s_axis_tvalid;
    wire s_axis_tready;
    reg s_axis_tlast;

    wire [31:0] m_axis_tdata;
    wire [3:0] m_axis_tkeep;
    wire m_axis_tvalid;
    reg m_axis_tready;
    wire m_axis_tlast;

    wire input_bram_clk;
    wire input_bram_rst;
    wire input_bram_en;
    wire [3:0] input_bram_we;
    wire [31:0] input_bram_addr;
    wire [31:0] input_bram_din;
    reg [31:0] input_bram_dout;

    reg [15:0] input_mem [0:MAX_IN_FEATURES-1];
    reg [31:0] input_bram_mem [0:MAX_BRAM_WORDS-1];
    reg [31:0] input_bram_pipe [0:3];
    reg [7:0] packet_mem [0:MAX_PACKET_BYTES-1];
    reg signed [31:0] expected_out [0:LANES-1];
    reg signed [31:0] expected_block_acc [0:MAX_MODE1_OUTPUTS-1];
    reg signed [31:0] captured_out [0:MAX_MODE1_OUTPUTS-1];

    string case_name;
    string case_dir;
    string path;
    integer in_features_i;
    integer out_features_i;
    integer packet_bytes_i;
    integer blocks_per_row_i;
    integer expected_beats_i;
    integer expected_tlast_beat_i;
    integer expected_output_count;
    integer output_count;
    integer failure_count;
    integer mode1_enabled;
    integer active_sink;
    integer ready_cycle;
    integer no_backpressure;
    integer bram_latency_i;
    integer lane;
    integer block;
    integer cycle;

    gemv_q8_0_dma_top #(
        .C_S_AXI_DATA_WIDTH(32),
        .C_S_AXI_ADDR_WIDTH(12),
        .LANES(LANES),
        .Q8_BLOCK_SIZE(Q8_BLOCK_SIZE),
        .INPUT_ADDR_WIDTH(16),
        .VERSION(32'h000A_0001)
    ) dut (
        .S_AXI_ACLK(clk),
        .S_AXI_ARESETN(resetn),
        .S_AXI_AWADDR(s_axi_awaddr),
        .S_AXI_AWPROT(s_axi_awprot),
        .S_AXI_AWVALID(s_axi_awvalid),
        .S_AXI_AWREADY(s_axi_awready),
        .S_AXI_WDATA(s_axi_wdata),
        .S_AXI_WSTRB(s_axi_wstrb),
        .S_AXI_WVALID(s_axi_wvalid),
        .S_AXI_WREADY(s_axi_wready),
        .S_AXI_BRESP(s_axi_bresp),
        .S_AXI_BVALID(s_axi_bvalid),
        .S_AXI_BREADY(s_axi_bready),
        .S_AXI_ARADDR(s_axi_araddr),
        .S_AXI_ARPROT(s_axi_arprot),
        .S_AXI_ARVALID(s_axi_arvalid),
        .S_AXI_ARREADY(s_axi_arready),
        .S_AXI_RDATA(s_axi_rdata),
        .S_AXI_RRESP(s_axi_rresp),
        .S_AXI_RVALID(s_axi_rvalid),
        .S_AXI_RREADY(s_axi_rready),
        .S_AXIS_TDATA(s_axis_tdata),
        .S_AXIS_TKEEP(s_axis_tkeep),
        .S_AXIS_TVALID(s_axis_tvalid),
        .S_AXIS_TREADY(s_axis_tready),
        .S_AXIS_TLAST(s_axis_tlast),
        .M_AXIS_TDATA(m_axis_tdata),
        .M_AXIS_TKEEP(m_axis_tkeep),
        .M_AXIS_TVALID(m_axis_tvalid),
        .M_AXIS_TREADY(m_axis_tready),
        .M_AXIS_TLAST(m_axis_tlast),
        .INPUT_BRAM_CLK(input_bram_clk),
        .INPUT_BRAM_RST(input_bram_rst),
        .INPUT_BRAM_EN(input_bram_en),
        .INPUT_BRAM_WE(input_bram_we),
        .INPUT_BRAM_ADDR(input_bram_addr),
        .INPUT_BRAM_DIN(input_bram_din),
        .INPUT_BRAM_DOUT(input_bram_dout)
    );

    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    always @(posedge clk) begin
        if (!resetn || !active_sink) begin
            m_axis_tready <= 1'b0;
            ready_cycle <= 0;
        end else begin
            ready_cycle <= ready_cycle + 1;
            if (no_backpressure) begin
                m_axis_tready <= 1'b1;
            end else begin
                m_axis_tready <= !((ready_cycle % 7) == 3 || (ready_cycle % 11) == 5);
            end
        end
    end

    always @(posedge input_bram_clk) begin
        if (input_bram_rst) begin
            input_bram_pipe[0] <= 32'd0;
            input_bram_pipe[1] <= 32'd0;
            input_bram_pipe[2] <= 32'd0;
            input_bram_pipe[3] <= 32'd0;
            input_bram_dout <= 32'd0;
        end else begin
            if (input_bram_en) begin
                input_bram_pipe[0] <= input_bram_mem[input_bram_addr[31:2]];
            end
            input_bram_pipe[1] <= input_bram_pipe[0];
            input_bram_pipe[2] <= input_bram_pipe[1];
            input_bram_pipe[3] <= input_bram_pipe[2];
            input_bram_dout <= input_bram_pipe[bram_latency_i];
        end
    end

    always @(posedge clk) begin
        if (resetn && active_sink && m_axis_tvalid && m_axis_tready) begin
            check_output_word;
        end
    end

    task load_case;
        integer i;
        begin
            case_name = "B_64x16_P0";
            if (!$value$plusargs("CASE_DIR=%s", case_dir)) begin
                case_dir = "golden/s05_6_1_multiblock/B_64x16_P0";
            end
            in_features_i = 64;
            out_features_i = 16;
            packet_bytes_i = 1152;
            blocks_per_row_i = in_features_i / Q8_BLOCK_SIZE;
            expected_beats_i = packet_bytes_i / TKEEP_WIDTH;
            expected_tlast_beat_i = expected_beats_i - 1;

            path = {case_dir, "/input_i16.hex"};
            $readmemh(path, input_mem);
            path = {case_dir, "/packet_axis128.hex"};
            $readmemh(path, packet_mem);
            path = {case_dir, "/output_mode0_i32.hex"};
            $readmemh(path, expected_out);
            path = {case_dir, "/block_acc_i32.hex"};
            $readmemh(path, expected_block_acc);

            for (i = 0; i < MAX_BRAM_WORDS; i = i + 1) begin
                input_bram_mem[i] = 32'd0;
            end
            for (i = 0; i < in_features_i; i = i + 2) begin
                input_bram_mem[i / 2] = {input_mem[i + 1], input_mem[i]};
            end
            if (packet_bytes_i > MAX_PACKET_BYTES) begin
                $fatal(1, "[FAIL] packet too large: %0d", packet_bytes_i);
            end
        end
    endtask

    task reset_dut;
        integer i;
        begin
            resetn = 1'b0;
            s_axi_awaddr = 12'd0;
            s_axi_awprot = 3'd0;
            s_axi_awvalid = 1'b0;
            s_axi_wdata = 32'd0;
            s_axi_wstrb = 4'hf;
            s_axi_wvalid = 1'b0;
            s_axi_bready = 1'b0;
            s_axi_araddr = 12'd0;
            s_axi_arprot = 3'd0;
            s_axi_arvalid = 1'b0;
            s_axi_rready = 1'b0;
            s_axis_tdata = 128'd0;
            s_axis_tkeep = 16'hffff;
            s_axis_tvalid = 1'b0;
            s_axis_tlast = 1'b0;
            m_axis_tready = 1'b0;
            active_sink = 0;
            ready_cycle = 0;
            output_count = 0;
            failure_count = 0;
            for (i = 0; i < MAX_MODE1_OUTPUTS; i = i + 1) begin
                captured_out[i] = 32'sd0;
            end
            repeat (12) @(posedge clk);
            resetn = 1'b1;
            repeat (8) @(posedge clk);
        end
    endtask

    task axi_write;
        input [11:0] addr;
        input [31:0] data;
        begin
            @(posedge clk);
            s_axi_awaddr <= addr;
            s_axi_wdata <= data;
            s_axi_wstrb <= 4'hf;
            s_axi_awvalid <= 1'b1;
            s_axi_wvalid <= 1'b1;
            s_axi_bready <= 1'b1;
            do begin
                @(posedge clk);
            end while (!(s_axi_awready && s_axi_wready));
            s_axi_awvalid <= 1'b0;
            s_axi_wvalid <= 1'b0;
            do begin
                @(posedge clk);
            end while (!s_axi_bvalid);
            if (s_axi_bresp !== 2'b00) begin
                failure_count = failure_count + 1;
                $display("[FAIL] AXI write BRESP addr=0x%03x resp=%0d", addr, s_axi_bresp);
            end
            @(posedge clk);
            s_axi_bready <= 1'b0;
        end
    endtask

    task axi_read;
        input [11:0] addr;
        output [31:0] data;
        begin
            @(posedge clk);
            s_axi_araddr <= addr;
            s_axi_arvalid <= 1'b1;
            s_axi_rready <= 1'b1;
            do begin
                @(posedge clk);
            end while (!s_axi_arready);
            s_axi_arvalid <= 1'b0;
            do begin
                @(posedge clk);
            end while (!s_axi_rvalid);
            data = s_axi_rdata;
            if (s_axi_rresp !== 2'b00) begin
                failure_count = failure_count + 1;
                $display("[FAIL] AXI read RRESP addr=0x%03x resp=%0d", addr, s_axi_rresp);
            end
            @(posedge clk);
            s_axi_rready <= 1'b0;
        end
    endtask

    task clear_core_like_c_driver;
        reg [31:0] unused;
        begin
            axi_write(GEMV_CONTROL, 32'h00000002);
            axi_write(GEMV_DONE, 32'h00000001);
            axi_read(GEMV_STATUS, unused);
            axi_read(GEMV_DONE, unused);
        end
    endtask

    task configure_like_c_driver;
        input integer mode_value;
        input integer result_bytes;
        begin
            clear_core_like_c_driver;
            axi_write(GEMV_MODE, mode_value[31:0]);
            axi_write(GEMV_SCALE_SHIFT, SCALE_SHIFT[31:0]);
            axi_write(GEMV_IN_FEATURES, in_features_i[31:0]);
            axi_write(GEMV_OUT_FEATURES, out_features_i[31:0]);
            axi_write(GEMV_INPUT_BASE, 32'h42000000);
            axi_write(GEMV_WEIGHT_LENGTH, packet_bytes_i[31:0]);
            axi_write(GEMV_RESULT_LENGTH, result_bytes[31:0]);
        end
    endtask

    task send_beat;
        input [127:0] data;
        input last;
        begin
            @(posedge clk);
            s_axis_tdata <= data;
            s_axis_tkeep <= 16'hffff;
            s_axis_tlast <= last;
            s_axis_tvalid <= 1'b1;
            do begin
                @(posedge clk);
            end while (!s_axis_tready);
            s_axis_tvalid <= 1'b0;
            s_axis_tlast <= 1'b0;
            s_axis_tdata <= 128'd0;
        end
    endtask

    task drive_packet;
        integer off;
        integer b;
        integer beat_index;
        reg [127:0] beat;
        begin
            beat_index = 0;
            for (off = 0; off < packet_bytes_i; off = off + TKEEP_WIDTH) begin
                beat = 128'd0;
                for (b = 0; b < TKEEP_WIDTH; b = b + 1) begin
                    beat[b*8 +: 8] = packet_mem[off + b];
                end
                send_beat(beat, beat_index == expected_tlast_beat_i);
                beat_index = beat_index + 1;
            end
        end
    endtask

    task check_output_word;
        reg signed [31:0] expected;
        reg expected_last;
        integer expected_lane;
        integer expected_block;
        begin
            if (output_count >= expected_output_count) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s mode%0d extra output idx=%0d data=%0d tlast=%0b",
                         case_name, mode1_enabled, output_count, $signed(m_axis_tdata), m_axis_tlast);
            end else begin
                if (m_axis_tkeep !== 4'hf) begin
                    failure_count = failure_count + 1;
                    $display("[FAIL] %0s mode%0d M_AXIS_TKEEP idx=%0d got=0x%01x expected=0xf",
                             case_name, mode1_enabled, output_count, m_axis_tkeep);
                end
                if (mode1_enabled) begin
                    expected = expected_block_acc[output_count];
                    expected_block = output_count / LANES;
                    expected_lane = output_count % LANES;
                end else begin
                    expected = expected_out[output_count];
                    expected_block = 0;
                    expected_lane = output_count;
                end
                expected_last = (output_count == expected_output_count - 1);
                captured_out[output_count] = m_axis_tdata;
                if (m_axis_tlast !== expected_last) begin
                    failure_count = failure_count + 1;
                    $display("[FAIL] %0s mode%0d TLAST idx=%0d got=%0b expected=%0b",
                             case_name, mode1_enabled, output_count, m_axis_tlast, expected_last);
                end
                if ($signed(m_axis_tdata) !== expected) begin
                    failure_count = failure_count + 1;
                    $display("[FAIL] %0s mode%0d result idx=%0d block=%0d lane=%0d got=%0d expected=%0d",
                             case_name, mode1_enabled, output_count, expected_block, expected_lane,
                             $signed(m_axis_tdata), expected);
                end
                if (expected_lane == 4 || expected_lane == 12) begin
                    $display("LANE_FOCUS,%0s,mode%0d,block=%0d,lane=%0d,got=%0d,expected=%0d,tlast=%0b",
                             case_name, mode1_enabled, expected_block, expected_lane,
                             $signed(m_axis_tdata), expected, m_axis_tlast);
                end
            end
            output_count = output_count + 1;
        end
    endtask

    task verify_status_debug;
        input integer mode_value;
        reg [31:0] status;
        reg [31:0] error_code;
        reg [31:0] debug_row;
        reg [31:0] debug_block;
        reg [31:0] debug_lane;
        reg [31:0] debug_in_count;
        reg [31:0] debug_tlast_count;
        reg [31:0] debug_tlast_tkeep;
        begin
            axi_read(GEMV_STATUS, status);
            axi_read(GEMV_ERROR_CODE, error_code);
            axi_read(GEMV_DEBUG_ROW, debug_row);
            axi_read(GEMV_DEBUG_BLOCK, debug_block);
            axi_read(GEMV_DEBUG_LANE, debug_lane);
            axi_read(GEMV_DEBUG_IN_COUNT, debug_in_count);
            axi_read(GEMV_DEBUG_TLAST_COUNT, debug_tlast_count);
            axi_read(GEMV_DEBUG_TLAST_TKEEP, debug_tlast_tkeep);

            $display("STATUS,%0s,mode%0d,status=0x%08x,error=0x%08x,debug_row=%0d,debug_block=%0d,debug_lane=%0d,debug_in_count=%0d,debug_tlast_count=%0d,debug_tlast_tkeep=0x%08x",
                     case_name, mode_value, status, error_code, debug_row, debug_block, debug_lane,
                     debug_in_count, debug_tlast_count, debug_tlast_tkeep);
            if (!status[1] || status[2] || error_code != 32'd0) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s mode%0d bad status/error status=0x%08x error=0x%08x",
                         case_name, mode_value, status, error_code);
            end
            if (debug_in_count != expected_beats_i[31:0]) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s mode%0d debug_in_count=%0d expected=%0d",
                         case_name, mode_value, debug_in_count, expected_beats_i);
            end
            if (debug_tlast_count != expected_tlast_beat_i[31:0]) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s mode%0d debug_tlast_count=%0d expected=%0d",
                         case_name, mode_value, debug_tlast_count, expected_tlast_beat_i);
            end
            if (debug_tlast_tkeep[15:0] != 16'hffff) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s mode%0d debug_tlast_tkeep=0x%08x expected low 0xffff",
                         case_name, mode_value, debug_tlast_tkeep);
            end
        end
    endtask

    task run_one_mode;
        input integer mode_value;
        integer result_bytes;
        reg [31:0] status;
        reg [31:0] build_config;
        begin
            mode1_enabled = mode_value;
            output_count = 0;
            active_sink = 0;
            ready_cycle = 0;
            result_bytes = mode_value ? blocks_per_row_i * LANES * 4 : LANES * 4;
            expected_output_count = mode_value ? blocks_per_row_i * LANES : LANES;

            $display("[CASE] %0s mode=%0d in=%0d out=%0d blocks=%0d packet_bytes=%0d beats=%0d expected_outputs=%0d bram_latency=%0d backpressure=%0s",
                     case_name, mode_value, in_features_i, out_features_i, blocks_per_row_i,
                     packet_bytes_i, expected_beats_i, expected_output_count, bram_latency_i,
                     no_backpressure ? "off" : "on");

            configure_like_c_driver(mode_value, result_bytes);
            axi_read(GEMV_BUILD_CONFIG, build_config);
            if (build_config != 32'h00800010) begin
                failure_count = failure_count + 1;
                $display("[FAIL] build_config=0x%08x expected=0x00800010", build_config);
            end

            active_sink = 1;
            axi_write(GEMV_START, 32'h00000001);

            fork
                drive_packet;
            join

            cycle = 0;
            do begin
                axi_read(GEMV_STATUS, status);
                cycle = cycle + 1;
            end while (cycle < TIMEOUT_CYCLES && !status[1] && !status[2]);
            repeat (16) @(posedge clk);
            active_sink = 0;

            if (cycle >= TIMEOUT_CYCLES) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s mode%0d timeout output_count=%0d", case_name, mode_value, output_count);
            end
            if (output_count != expected_output_count) begin
                failure_count = failure_count + 1;
                $display("[FAIL] %0s mode%0d output_count=%0d expected=%0d",
                         case_name, mode_value, output_count, expected_output_count);
            end
            verify_status_debug(mode_value);
        end
    endtask

    initial begin
        no_backpressure = 0;
        bram_latency_i = 2;
        if ($test$plusargs("NO_BACKPRESSURE")) begin
            no_backpressure = 1;
        end
        void'($value$plusargs("BRAM_LATENCY=%d", bram_latency_i));
        if (bram_latency_i < 0 || bram_latency_i > 3) begin
            $fatal(1, "[FAIL] BRAM_LATENCY must be 0..3, got %0d", bram_latency_i);
        end

        load_case;
        reset_dut;
        run_one_mode(0);
        run_one_mode(1);

        if (failure_count == 0) begin
            $display("S05_6_2_DMA_TOP_MULTIBLOCK_PASS %0s", case_name);
            $finish;
        end else begin
            $display("S05_6_2_DMA_TOP_MULTIBLOCK_FAIL %0s failures=%0d", case_name, failure_count);
            $fatal(1, "S05.6.2 dma_top wrapper multi-block failed");
        end
    end
endmodule
