# PL standalone tests

Run the control-plane tests from a shell with Vivado on `PATH`:

```powershell
xvlog.bat tests/pl/tb_reg_config.v `
  DMA_2026_1012/DMA_2026_1012.srcs/sources_1/new/axi_bram_ctrl.v `
  DMA_2026_1012/DMA_2026_1012.srcs/sources_1/new/bram_ctrl_intf.v `
  DMA_2026_1012/DMA_2026_1012.srcs/sources_1/new/reg_ctrl.v
xelab.bat work.tb_reg_config -s tb_reg_config_sim
xsim.bat tb_reg_config_sim -runall

xvlog.bat tests/pl/tb_config_latch.v `
  DMA_2026_1012/DMA_2026_1012.srcs/sources_1/new/pl_config_latch.v
xelab.bat work.tb_config_latch -s tb_config_latch_sim
xsim.bat tb_config_latch_sim -runall

xvlog.bat tests/pl/tb_config_pipeline.v `
  DMA_2026_1012/DMA_2026_1012.srcs/sources_1/new/axi_bram_ctrl.v `
  DMA_2026_1012/DMA_2026_1012.srcs/sources_1/new/bram_ctrl_intf.v `
  DMA_2026_1012/DMA_2026_1012.srcs/sources_1/new/reg_ctrl.v `
  DMA_2026_1012/DMA_2026_1012.srcs/sources_1/new/pl_config_latch.v
xelab.bat work.tb_config_pipeline -s tb_config_pipeline_sim
xsim.bat tb_config_pipeline_sim -runall
```

Expected outputs:

```text
PASS: reg_ctrl shadow/commit/status path passed
PASS: pl_config_latch frame-boundary sequencing passed
PASS: reg_ctrl -> commit snapshot -> frame-boundary pipeline passed
```

Tests:

- `tb_reg_config`: staging registers, atomic commit snapshot, `CONFIG_BUSY` /
  `ACTIVE_EPOCH` readback, and reserved `0x208` / `0x21C` rejection.
- `tb_config_latch`: frame-in-progress commit, soft/DDS/hard classification,
  single-cycle CIC/DDS commands, and latest-commit-wins behavior.
- `tb_config_pipeline`: the exact `reg_ctrl -> pl_config_latch` wiring used by
  `logic_top`, without DMA/FFT/RFDC.
- `DDS_RESYNC` and `CIC_VALID` are PL-owned. The PS must never write them.

The tests stub `ila_bram_ctrl_intf`; they do not replace the real ILA in the
Vivado project.

## Wideband CIC bypass tests

These tests use the real Poly/CIC/HB/FFT IP blocks and must be run through the
Vivado project:

```text
tests/pl/tb_dec_bypass.v           CIC=1 bypass valid rate, CIC=4 normal rate
tests/pl/tb_wideband_fft_smoke.v   312.5 MSPS continuous FFT/组帧 smoke
```

Expected:

```text
PASS: CIC bypass valid rate 512/512
PASS: CIC=4 valid rate ~125/512
PASS: wideband FFT throughput, ... frames>=2
```

The smoke test keeps `m_axis_data_tready=1` and checks
`fft_fifo_overflow_sticky=0`. At 312.5 MSPS the design uses 0% overlap because
the single 1024-point FFT cannot read a new 75%-overlap frame every 256 clocks.
