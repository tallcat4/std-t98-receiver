# std-t98-receiver

Windows PC + RTL-SDRで、STD-T98の1チャネルを`.burst`へ保存するCLIです。DSPはSPEAR由来です。

## 主な機能

- **RTL-SDR受信**：CH1〜30から指定した1チャネルを復調し、AMBEフレームを保存します。
- **IQファイルの再処理**：保存済みのIQから復調できます。SDRを接続せず、受信条件の比較やデバッグに使えます。
- **3種類の出力**：バースト境界を持つ`.burst`、互換用の`.ambe`、soft FECに使う信頼度情報`.llr`を出力します。
- **受信条件の調整**：デバイス、ゲイン、サンプルレート、周波数誤差を指定できます。
- **受信状態の確認**：同期状態・バースト数・CRC正常数を表示します。時間指定での停止にも対応します。

## セットアップ

64-bit WindowsとMinGW（C++20対応）が必要です。
USBドライバーは[Zadig](https://zadig.akeo.ie/)でRTL-SDRのInterface 0にWinUSBを設定します。

```powershell
.\build.ps1
.\setup-rtl.ps1
```

## 使い方

```powershell
.\build\std-t98.exe --list-devices
# CH1を受信
.\build\std-t98.exe --channel 1 -o capture --llr
# ゲインを指定して60秒受信
.\build\std-t98.exe --channel 1 --gain 30 --seconds 60 -o capture60 --llr
# 保存済みIQを再処理
.\build\std-t98.exe --input recording.cf32 --sample-rate 1200000 --center-freq 350950000 --channel 1 -o replay
```

停止はCtrl+C。`capture.burst`と、soft FEC用の`capture.llr`を保存します。
`.ambe`も必要なら`--ambe`を追加します。その他のオプションは`--help`で確認できます。

IQ入力はI/Q交互のlittle-endian float32です。収録時のサンプルレートと中心周波数を指定してください。
周波数の補正には`--ppm`（ドングル）または`--freq-error HZ`（残留ずれ）を使います。

解析は[std-t98-cracker](https://github.com/tallcat4/std-t98-cracker)、音声化は[std-t98-vocode](https://github.com/tallcat4/std-t98-vocode)を使います。
[ファイル形式](FORMAT.md) / [DSPの由来](PROVENANCE.md)
