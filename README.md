# std-t98-receiver

Windows PC + RTL-SDRで、指定したSTD-T98チャネルを`.burst`へ保存する小さな開発用CLIです。
DSPはSPEARのC++受信処理を基にしています。GUI、音声合成、秘話解析、チャネル探索はありません。

## Windowsで使う

64-bit Windows、C++20対応MinGW（このPCではStrawberry Perl同梱GCC 13.2）で:

```powershell
.\build.ps1
.\setup-rtl.ps1
.\build\std-t98.exe --list-devices
.\build\std-t98.exe --channel 1 --output capture --llr
```

停止はCtrl+C。`capture.burst`が主出力、`--llr`を付けるとsoft FEC用の`capture.llr`も保存します。
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
レートは300 kHzまたは400 kHzの整数倍。通常は1.2 Msps、2.4 Msps。実機が受け付けない設定はエラーになります。

CMakeでもビルドできます（WindowsはVisual Studio C++20、LinuxはGCC/Clangとlibrtlsdr）:

```text
cmake -S . -B build-cmake
cmake --build build-cmake --config Release
ctest --test-dir build-cmake -C Release --output-on-failure
```

`build.ps1`は40組のSPEAR由来プロトコルベクトル、同期捕捉/喪失、PFB、出力形式を検証します。
3リポジトリを隣同士に配置して各ビルドを済ませれば、親フォルダから
`python std-t98-receiver/tests/test_family.py`で受け渡し、実ボコーダーWAV、模擬RTL DLL、日本語パスを検証できます（NumPyが必要）。
**実ドングルと実無線による受信感度・安定性の検証は未実施です。** 合成データ/模擬DLLの合格はRF実測の代わりにはなりません。

移植範囲と変更点は[PROVENANCE.md](PROVENANCE.md)。
