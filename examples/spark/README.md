# Spark integration examples

The board brief is not a final wiring contract. Choose the actual TSL254x variant
and bind the existing NSX I2C adapter to its endpoint; see [light sensor usage](../../docs/tsl254x.md).
Use separate bus/mux endpoints if another device shares address 0x39.

`t5838_pair.c` is a compiled and host-tested example of coordinating two T5838
contexts on one clock. Supply two THSEL waveform endpoints and a shared clock
callback, hold a group lock, stop capture and call `spark_t5838_configure_pair`.
Select Analog/D2 for clock-off wake detection, D1 for AAD plus low-power audio, or
OFF for ordinary low-power capture. The helper changes the final clock only after
both devices are configured and enabled; it makes no invented pin assignments.

For an NSX capture application, prepare a caller-owned `nsx_audio_config_t`:

```c
/* Keep config, buffers and callback valid until capture stops. */
nsx_audio_config_t capture = {0};
capture.source = NSX_AUDIO_SOURCE_PDM;
capture.num_channels = 2;
capture.num_samples = 128; /* Per channel, not a sample-rate request. */
capture.pdm = nsx_audio_pdm_default;
capture.pdm.mic = board_pdm_instance;
capture.pdm.clock_freq = NSX_AUDIO_PDM_CLK_750KHZ;
/* Set board-qualified clock source; assign 32-byte-aligned DMA storage
 * (>=128*2*8 bytes), PCM16 storage (>=128*2*2 bytes), sizes and callback. */
```

Initialize and start through `nsx_audio_init/start`, checking each status. The
above is a configuration sketch, not a complete firmware example. The audio
controller's start must be coordinated with the programming-clock owner: D1
cannot tolerate a >=70 us clock-low gap. If the board cannot provide a continuous
handoff, use ordinary AAD-OFF capture and treat D1 capture integration as pending.
The sensor helper does not start DMA or synthesize audio samples.

Defer WAKE events and PCM processing to a task. Ignore enable acknowledgement
pulses and startup transients, then qualify actual PCM sample rate, channel
ordering and acoustic thresholds on the board. On any shared-clock failure,
invalidate both contexts and recover the pair before resuming. See the
[T5838 contract](../../docs/t5838.md) for protocol, reset and timing details.

No Spark pinout, timer waveform emitter, high-quality NSX capture clock or hardware
qualification is supplied by these portable examples. Those depend on the final
BOM/schematic and board resources. The examples need no framework migration.
