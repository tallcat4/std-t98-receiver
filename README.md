# std-t98-receiver

Windows PC + RTL-SDRで、指定したSTD-T98チャネルを`.burst`へ保存する小さな開発用CLIです。
DSPはSPEARのC++受信処理を基にしています。GUI、音声合成、秘話解析、チャネル探索はありません。
RF/DSPはSPEAR由来のフィルタ・検波・同期処理を採用しています。
**指定した1チャネル専用**です。前段も選択チャネルの出力だけを計算し、全チャネルの抽出や同時復調は行いません。
SPEARのPFBと同じ係数・間引き位相・遅延を保ち、DCへ移した対象の成分だけを計算します。
受信フィルタの係数設計には引き続きSPEARと同じFFTWを使います。

## Windowsで使う

64-bit Windows、C++20対応MinGW（このPCではStrawberry Perl同梱GCC 13.2）で:

```powershell
.\build.ps1
.\setup-rtl.ps1
.\build\std-t98.exe --list-devices
.\build\std-t98.exe --channel 1 --output capture --llr
```

停止はCtrl+C。`capture.burst`が主出力、`--llr`を付けるとsoft FEC用の`capture.llr`も保存します。
`build.ps1`は[FFTW公式Windows配布](https://www.fftw.org/install/windows.html)のsingle-precision x64 DLLを
SHA-256確認後に取得します。実行時は`libfftw3f-3.dll`をexeと同じフォルダに置いてください。
`.ambe`が必要な既存ツールには`--ambe`を付けてください。既存ファイルには上書きしません。
CH1=351.20000 MHz、以降6.25 kHz刻みでCH30まで。周波数を走査せず、指定した1チャネルだけを処理します。

`setup-rtl.ps1`は[RTL-SDR Blog V1.4.0](https://github.com/rtlsdrblog/rtl-sdr-blog/releases/tag/V1.4.0)の
x64 DLLをSHA-256確認後に`build`へ配置します。USBドライバは変更しません。
RTL-SDRを初めて使うWindowsでは、[Zadig](https://zadig.akeo.ie/)で対象ドングルの **Interface 0** にWinUSBを設定してください。
ドングル未接続なら`--list-devices`は一覧が空になります。

受信の開始例（gain/ppmは実機に合わせる）:

```powershell
.\build\std-t98.exe --channel 1 --gain 30 --ppm 0 --seconds 60 -o capture --llr
```

画面の`SYNC`、`bursts`、`CRC OK`を確認します。`WAIT`のみ・0バーストなら、まずチャネル、アンテナ、gain、周波数誤差を確認してください。
`--ppm`はドングルの補正、`--freq-error HZ`は残留周波数ずれの手動補正です。正の値でDSPの受信位置が上へ移ります。
同じ出力名で再実行するとエラーになるので、別名を指定するか、明示的に`--overwrite`を付けてください。

## 3つの役割

| ツール | 入力 → 出力 |
|---|---|
| [std-t98-receiver](https://github.com/tallcat4/std-t98-receiver) | RTL-SDR / IQ → 72-bit `.burst` |
| [std-t98-cracker](https://github.com/tallcat4/std-t98-cracker) | `.burst` → 鍵候補のJSON、指定鍵で49-bit `.burst` |
| [std-t98-vocode](https://github.com/tallcat4/std-t98-vocode) | `.burst` / `.ambe` → WAV、明示的FEC変換 |

それぞれ手で実行する独立したCLIです。通常の引き継ぎには`.burst`を使います。形式は[FORMAT.md](FORMAT.md)。

```powershell
# 平文ならvocode側で
python -m std_t98_vocode decode ..\std-t98-receiver\capture.burst -o clear.wav
# 秘話の解析はcracker側で明示的に
python -m std_t98_cracker crack ..\std-t98-receiver\capture.burst --method linear --llr ..\std-t98-receiver\capture.llr
```

## IQリプレイ・ビルド・検証

```powershell
.\build\std-t98.exe --input recording.cf32 --sample-rate 1200000 --center-freq 350950000 --channel 1 -o replay
```

IQ形式はlittle-endian float32のI,Q交互（1サンプル8 byte）。`.sigmf-data`でも中身がcf32_leなら読めますが、
メタデータは読みません。sample rateと中心周波数を必ず指定してください。
既定のポリフェーズ間引きではレートは400 kHzの整数倍。通常は1.2 Msps、2.4 Msps。実機が受け付けない設定はエラーになります。
旧構成を比較する場合だけ`--pfb 48`を指定でき、その場合は300 kHzの整数倍です。

CMakeでもビルドできます。FFTWのsingle-precision開発ライブラリが必要です。
Windows MinGWでは先に`setup-fftw.ps1`を実行します。MSVCでは取得したdefから
`lib /machine:x64 /def:libfftw3f-3.def`でimport libraryを作り、`build/fftw`へ置いてください。
LinuxはGCC/ClangとFFTW3 single-precision開発パッケージ、受信時にlibrtlsdrを使用します。

```text
cmake -S . -B build-cmake
cmake --build build-cmake --config Release
ctest --test-dir build-cmake -C Release --output-on-failure
```

`build.ps1`は40組のSPEAR由来プロトコルベクトル、同期捕捉/喪失、PFB、出力形式を検証します。
3リポジトリを隣同士に配置して各ビルドを済ませれば、親フォルダから
`python std-t98-receiver/tests/test_family.py`で受け渡し、実ボコーダーWAV、模擬RTL DLL、日本語パスを検証できます（NumPyが必要）。
2026-10-23、Windows + RTL-SDR Blog V4 + 実無線機（CH1・秘話OFF）で記録したIQを検証しました。
1.2 Msps、gain 19.7 dB、残留ずれ `--freq-error -330` の手動補正で、
9.92秒分の124バーストを保存、SACCH CRCは124件正常、音声496フレームすべてがFEC検査を通過しました。
vocodeでWAV生成まで確認しています。聴感品質、受信感度、長時間安定性は別途確認が必要です。
この補正値は試験時の機器の組み合わせで得た値であり、他の機器へ一律に適用する値ではありません。

移植範囲と変更点は[PROVENANCE.md](PROVENANCE.md)。
