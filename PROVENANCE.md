# DSPの由来と移植差分

基準: SPEAR `d1611fd408939ad238372d58828639e8fde28400`。SPEARの作業ツリーは変更していません。

`vendor/spear`には受信器、プロトコル、RRC設計、Gardner symbol sync、同期状態機械、FIR、PFBおよび40組のgolden vectorを収録。
GNU Radio、Qt、UHD、SPEARの統合サービスには依存しません。

- CLIは1チャネルを明示選択。`num_channels=1`で受信周波数をPFBのbin 0へ移します。
- PFBとRRC設計もSPEARのソースをそのまま採用し、同じsingle-precision FFTW APIを使用します。
  初版の自前DFT/IFFT置換は撤回しました。Windows用FFTWは公式配布3.3.5のx64 DLLです。
- 既定PFBは現行SPEAR Appと同じ64分割、前段帯域400 kHz。入力はRTL-SDR向けに1.2 Mspsから3分の1へ間引きます
  （SPEAR Appは4 Mspsから10分の1）。それ以降の6250 Hz/bin、10倍補間、受信フィルタ、同期処理は共通です。
  48分割は比較・旧IQ検証用の明示的な`--pfb 48`でのみ使用します。
- GCC `target_clones`はWindowsで無効化。coreの依存の代わりに`cf32`の型定義だけを供給。
- 入力はlibrtlsdrまたはcf32_le。出力フレーミングは旧std-t98-toolsの初期形式。
- 同期・CRCによる捕捉、tentative、flywheel、喪失の判定はSPEARのまま。電力スケルチでDSPや出力を止めません。

公開リポジトリに実録音は含めていません。RF感度や受信品質は実機で別途検証が必要です。
GPL-3.0のライセンス本文はLICENSE。

`tests/spear_sources.json`にSPEAR原本のLF正規化SHA-256を保存しています。
`python -m unittest discover -s tests -p test_spear_sources.py -v`で、FIRのWindows用属性ガード以外に
RF/DSPソースの差分がないことを検査できます。FFTWの版やCPU最適化により数値の微小な丸め差はあり得ます。
