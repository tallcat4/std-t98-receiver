# DSPの由来と移植差分

基準: SPEAR `d1611fd408939ad238372d58828639e8fde28400`。SPEARの作業ツリーは変更していません。

`vendor/spear`には受信器、プロトコル、RRC設計、Gardner symbol sync、同期状態機械、FIR、PFBおよび40組のgolden vectorを収録。
GNU Radio、Qt、UHD、SPEARの統合サービスには依存しません。

- CLIは1チャネルを明示選択。受信器自体も`num_channels=1`以外を拒否します。
- `src/center_channelizer.hpp`はSPEARのPFBと同じプロトタイプ係数・commutator・遅延線でbin 0だけを計算します。
  選択周波数は事前にDCへ移します。bin 0の逆FFT係数はすべて1なので、分枝の総和で等価な出力が得られます。
  全ビンのFFTも他チャネルの出力配列も生成しません。SPEAR原本のPFBは比較テスト用に保持しています。
- RRC設計はSPEARのソースをそのまま採用し、同じsingle-precision FFTW APIを使用します。
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
`python -m unittest discover -s tests -p test_spear_sources.py -v`で、原本および記録した受信器の単一チャネル化差分を検査します。
FIRにはWindows用属性ガードがあります。単一チャネルフィルタは原本PFBのbin 0との乱数入力比較でも検証します。
和の計算順序、FFTWの版やCPU最適化により数値の微小な丸め差はあり得ます。
