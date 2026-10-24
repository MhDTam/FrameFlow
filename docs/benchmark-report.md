# FrameFlow timing benchmark

Measured by `FrameFlow.Benchmark.exe` on this machine. QPC 10000000 Hz, 16 logical CPUs, 8 s per case, simulated work 0..0% of the frame.

| Limiter | Target FPS | Avg ms | Avg FPS | Stddev ms | P95 dev ms | P99 dev ms | Max dev ms | Late | CPU (1 core) | Awake-wait |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| FramePacer/Efficiency | 30 | 33.3333 | 30.000 | 0.0032 | 0.0001 | 0.0132 | 0.0316 | 1 | 2.15% | 2.14% |
| FramePacer/Efficiency | 60 | 16.6667 | 60.000 | 0.0028 | 0.0001 | 0.0076 | 0.0411 | 0 | 4.22% | 4.20% |
| FramePacer/Efficiency | 120 | 8.3333 | 120.000 | 0.0071 | 0.0001 | 0.0463 | 0.0814 | 0 | 8.47% | 8.43% |
| FramePacer/Efficiency | 144 | 6.9444 | 144.000 | 0.0026 | 0.0001 | 0.0094 | 0.0367 | 0 | 8.22% | 8.25% |
| FramePacer/Efficiency | 165 | 6.0606 | 165.000 | 0.0063 | 0.0001 | 0.0144 | 0.1279 | 0 | 11.97% | 11.91% |
| FramePacer/Efficiency | 240 | 4.1667 | 240.000 | 0.0009 | 0.0001 | 0.0010 | 0.0175 | 0 | 17.59% | 17.51% |
| FramePacer/Balanced | 30 | 33.3333 | 30.000 | 0.0008 | 0.0008 | 0.0043 | 0.0066 | 0 | 2.50% | 2.51% |
| FramePacer/Balanced | 60 | 16.6667 | 60.000 | 0.0001 | 0.0001 | 0.0001 | 0.0009 | 0 | 5.35% | 5.33% |
| FramePacer/Balanced | 120 | 8.3333 | 120.000 | 0.0096 | 0.0001 | 0.0060 | 0.1751 | 0 | 10.35% | 10.30% |
| FramePacer/Balanced | 144 | 6.9444 | 144.000 | 0.0079 | 0.0001 | 0.0016 | 0.1857 | 0 | 11.28% | 11.18% |
| FramePacer/Balanced | 165 | 6.0606 | 165.000 | 0.0028 | 0.0001 | 0.0047 | 0.0519 | 0 | 15.29% | 15.27% |
| FramePacer/Balanced | 240 | 4.1667 | 240.000 | 0.0009 | 0.0001 | 0.0013 | 0.0186 | 0 | 21.63% | 21.62% |
| FramePacer/Precision | 30 | 33.3333 | 30.000 | 0.0025 | 0.0001 | 0.0124 | 0.0242 | 0 | 3.26% | 3.26% |
| FramePacer/Precision | 60 | 16.6667 | 60.000 | 0.0003 | 0.0001 | 0.0014 | 0.0029 | 0 | 6.71% | 6.70% |
| FramePacer/Precision | 120 | 8.3333 | 120.000 | 0.0026 | 0.0001 | 0.0018 | 0.0354 | 0 | 13.18% | 13.16% |
| FramePacer/Precision | 144 | 6.9444 | 144.000 | 0.0025 | 0.0001 | 0.0028 | 0.0516 | 0 | 15.22% | 15.21% |
| FramePacer/Precision | 165 | 6.0606 | 165.000 | 0.0015 | 0.0001 | 0.0065 | 0.0245 | 0 | 19.50% | 19.46% |
| FramePacer/Precision | 240 | 4.1667 | 240.000 | 0.0035 | 0.0001 | 0.0014 | 0.0750 | 0 | 26.31% | 26.34% |
| Naive Sleep() baseline | 60 | 30.8971 | 32.365 | 0.1846 | 14.3852 | 14.4439 | 14.4961 | 0 | 0.03% | 0.00% |
| Naive Sleep() baseline | 144 | 15.4600 | 64.683 | 0.1053 | 8.6169 | 8.6632 | 8.6895 | 0 | 0.05% | 0.00% |

## Detail

```
Target FPS: 30  [FramePacer/Efficiency]
Target frametime: 33.3333 ms
Frames measured: 240
Average: 33.3333 ms (30.000 FPS)
Median: 33.3333 ms
Std deviation: 0.0032 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0132 ms
Max deviation: 0.0316 ms
Release lateness vs deadline: P99 4.8 us, max 31.6 us
Late/rebased frames: 1
CPU overhead: 2.15% of one core (0.134% of total CPU); pacer awake-wait 2.14%
Adaptive timer margin at end: 946 us

Target FPS: 60  [FramePacer/Efficiency]
Target frametime: 16.6667 ms
Frames measured: 480
Average: 16.6667 ms (60.000 FPS)
Median: 16.6667 ms
Std deviation: 0.0028 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0076 ms
Max deviation: 0.0411 ms
Release lateness vs deadline: P99 3.2 us, max 41.0 us
Late/rebased frames: 0
CPU overhead: 4.22% of one core (0.264% of total CPU); pacer awake-wait 4.20%
Adaptive timer margin at end: 878 us

Target FPS: 120  [FramePacer/Efficiency]
Target frametime: 8.3333 ms
Frames measured: 960
Average: 8.3333 ms (120.000 FPS)
Median: 8.3333 ms
Std deviation: 0.0071 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0463 ms
Max deviation: 0.0814 ms
Release lateness vs deadline: P99 7.6 us, max 81.4 us
Late/rebased frames: 0
CPU overhead: 8.47% of one core (0.530% of total CPU); pacer awake-wait 8.43%
Adaptive timer margin at end: 950 us

Target FPS: 144  [FramePacer/Efficiency]
Target frametime: 6.9444 ms
Frames measured: 1152
Average: 6.9444 ms (144.000 FPS)
Median: 6.9444 ms
Std deviation: 0.0026 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0094 ms
Max deviation: 0.0367 ms
Release lateness vs deadline: P99 5.4 us, max 36.7 us
Late/rebased frames: 0
CPU overhead: 8.22% of one core (0.514% of total CPU); pacer awake-wait 8.25%
Adaptive timer margin at end: 878 us

Target FPS: 165  [FramePacer/Efficiency]
Target frametime: 6.0606 ms
Frames measured: 1320
Average: 6.0606 ms (165.000 FPS)
Median: 6.0606 ms
Std deviation: 0.0063 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0144 ms
Max deviation: 0.1279 ms
Release lateness vs deadline: P99 6.2 us, max 127.9 us
Late/rebased frames: 0
CPU overhead: 11.97% of one core (0.748% of total CPU); pacer awake-wait 11.91%
Adaptive timer margin at end: 1000 us

Target FPS: 240  [FramePacer/Efficiency]
Target frametime: 4.1667 ms
Frames measured: 1920
Average: 4.1667 ms (240.000 FPS)
Median: 4.1667 ms
Std deviation: 0.0009 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0010 ms
Max deviation: 0.0175 ms
Release lateness vs deadline: P99 0.0 us, max 17.4 us
Late/rebased frames: 0
CPU overhead: 17.59% of one core (1.100% of total CPU); pacer awake-wait 17.51%
Adaptive timer margin at end: 1000 us

Target FPS: 30  [FramePacer/Balanced]
Target frametime: 33.3333 ms
Frames measured: 240
Average: 33.3333 ms (30.000 FPS)
Median: 33.3333 ms
Std deviation: 0.0008 ms
P95 deviation: 0.0008 ms
P99 deviation: 0.0043 ms
Max deviation: 0.0066 ms
Release lateness vs deadline: P99 2.9 us, max 6.6 us
Late/rebased frames: 0
CPU overhead: 2.50% of one core (0.156% of total CPU); pacer awake-wait 2.51%
Adaptive timer margin at end: 1071 us

Target FPS: 60  [FramePacer/Balanced]
Target frametime: 16.6667 ms
Frames measured: 480
Average: 16.6667 ms (60.000 FPS)
Median: 16.6667 ms
Std deviation: 0.0001 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0001 ms
Max deviation: 0.0009 ms
Release lateness vs deadline: P99 0.0 us, max 0.8 us
Late/rebased frames: 0
CPU overhead: 5.35% of one core (0.334% of total CPU); pacer awake-wait 5.33%
Adaptive timer margin at end: 1079 us

Target FPS: 120  [FramePacer/Balanced]
Target frametime: 8.3333 ms
Frames measured: 960
Average: 8.3333 ms (120.000 FPS)
Median: 8.3333 ms
Std deviation: 0.0096 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0060 ms
Max deviation: 0.1751 ms
Release lateness vs deadline: P99 0.0 us, max 175.0 us
Late/rebased frames: 0
CPU overhead: 10.35% of one core (0.647% of total CPU); pacer awake-wait 10.30%
Adaptive timer margin at end: 1139 us

Target FPS: 144  [FramePacer/Balanced]
Target frametime: 6.9444 ms
Frames measured: 1152
Average: 6.9444 ms (144.000 FPS)
Median: 6.9444 ms
Std deviation: 0.0079 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0016 ms
Max deviation: 0.1857 ms
Release lateness vs deadline: P99 0.4 us, max 185.6 us
Late/rebased frames: 0
CPU overhead: 11.28% of one core (0.705% of total CPU); pacer awake-wait 11.18%
Adaptive timer margin at end: 905 us

Target FPS: 165  [FramePacer/Balanced]
Target frametime: 6.0606 ms
Frames measured: 1320
Average: 6.0606 ms (165.000 FPS)
Median: 6.0606 ms
Std deviation: 0.0028 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0047 ms
Max deviation: 0.0519 ms
Release lateness vs deadline: P99 0.2 us, max 51.9 us
Late/rebased frames: 0
CPU overhead: 15.29% of one core (0.956% of total CPU); pacer awake-wait 15.27%
Adaptive timer margin at end: 1212 us

Target FPS: 240  [FramePacer/Balanced]
Target frametime: 4.1667 ms
Frames measured: 1920
Average: 4.1667 ms (240.000 FPS)
Median: 4.1667 ms
Std deviation: 0.0009 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0013 ms
Max deviation: 0.0186 ms
Release lateness vs deadline: P99 0.3 us, max 18.6 us
Late/rebased frames: 0
CPU overhead: 21.63% of one core (1.352% of total CPU); pacer awake-wait 21.62%
Adaptive timer margin at end: 1110 us

Target FPS: 30  [FramePacer/Precision]
Target frametime: 33.3333 ms
Frames measured: 240
Average: 33.3333 ms (30.000 FPS)
Median: 33.3333 ms
Std deviation: 0.0025 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0124 ms
Max deviation: 0.0242 ms
Release lateness vs deadline: P99 0.1 us, max 24.1 us
Late/rebased frames: 0
CPU overhead: 3.26% of one core (0.204% of total CPU); pacer awake-wait 3.26%
Adaptive timer margin at end: 1323 us

Target FPS: 60  [FramePacer/Precision]
Target frametime: 16.6667 ms
Frames measured: 480
Average: 16.6667 ms (60.000 FPS)
Median: 16.6667 ms
Std deviation: 0.0003 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0014 ms
Max deviation: 0.0029 ms
Release lateness vs deadline: P99 0.3 us, max 2.9 us
Late/rebased frames: 0
CPU overhead: 6.71% of one core (0.419% of total CPU); pacer awake-wait 6.70%
Adaptive timer margin at end: 1377 us

Target FPS: 120  [FramePacer/Precision]
Target frametime: 8.3333 ms
Frames measured: 960
Average: 8.3333 ms (120.000 FPS)
Median: 8.3333 ms
Std deviation: 0.0026 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0018 ms
Max deviation: 0.0354 ms
Release lateness vs deadline: P99 0.0 us, max 35.4 us
Late/rebased frames: 0
CPU overhead: 13.18% of one core (0.823% of total CPU); pacer awake-wait 13.16%
Adaptive timer margin at end: 1348 us

Target FPS: 144  [FramePacer/Precision]
Target frametime: 6.9444 ms
Frames measured: 1152
Average: 6.9444 ms (144.000 FPS)
Median: 6.9444 ms
Std deviation: 0.0025 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0028 ms
Max deviation: 0.0516 ms
Release lateness vs deadline: P99 0.0 us, max 51.6 us
Late/rebased frames: 0
CPU overhead: 15.22% of one core (0.951% of total CPU); pacer awake-wait 15.21%
Adaptive timer margin at end: 1215 us

Target FPS: 165  [FramePacer/Precision]
Target frametime: 6.0606 ms
Frames measured: 1320
Average: 6.0606 ms (165.000 FPS)
Median: 6.0606 ms
Std deviation: 0.0015 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0065 ms
Max deviation: 0.0245 ms
Release lateness vs deadline: P99 1.2 us, max 24.5 us
Late/rebased frames: 0
CPU overhead: 19.50% of one core (1.219% of total CPU); pacer awake-wait 19.46%
Adaptive timer margin at end: 1532 us

Target FPS: 240  [FramePacer/Precision]
Target frametime: 4.1667 ms
Frames measured: 1920
Average: 4.1667 ms (240.000 FPS)
Median: 4.1667 ms
Std deviation: 0.0035 ms
P95 deviation: 0.0001 ms
P99 deviation: 0.0014 ms
Max deviation: 0.0750 ms
Release lateness vs deadline: P99 0.0 us, max 74.9 us
Late/rebased frames: 0
CPU overhead: 26.31% of one core (1.644% of total CPU); pacer awake-wait 26.34%
Adaptive timer margin at end: 1351 us

Target FPS: 60  [Naive Sleep() baseline]
Target frametime: 16.6667 ms
Frames measured: 300
Average: 30.8971 ms (32.365 FPS)
Median: 30.9884 ms
Std deviation: 0.1846 ms
P95 deviation: 14.3852 ms
P99 deviation: 14.4439 ms
Max deviation: 14.4961 ms
Release lateness vs deadline: P99 0.0 us, max 0.0 us
Late/rebased frames: 0
CPU overhead: 0.03% of one core (0.002% of total CPU); pacer awake-wait 0.00%
Adaptive timer margin at end: 0 us

Target FPS: 144  [Naive Sleep() baseline]
Target frametime: 6.9444 ms
Frames measured: 720
Average: 15.4600 ms (64.683 FPS)
Median: 15.4960 ms
Std deviation: 0.1053 ms
P95 deviation: 8.6169 ms
P99 deviation: 8.6632 ms
Max deviation: 8.6895 ms
Release lateness vs deadline: P99 0.0 us, max 0.0 us
Late/rebased frames: 0
CPU overhead: 0.05% of one core (0.003% of total CPU); pacer awake-wait 0.00%
Adaptive timer margin at end: 0 us

```
