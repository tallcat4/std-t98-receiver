# DSPの由来と移植差分

基準: SPEAR `d1611fd408939ad238372d58828639e8fde28400`。SPEARの作業ツリーは変更していません。

`vendor/spear`には受信器、プロトコル、RRC設計、Gardner symbol sync、同期状態機械、FIR、PFBおよび40組のgolden vectorを収録。
GNU Radio、Qt、UHD、SPEARの統合サービスには依存しません。

- CLIは1チャネルを明示選択。`num_channels=1`で受信周波数をPFBのbin 0へ移します。
- FFTW依存を除去。PFBの48/64点逆変換は事前計算の複素DFT（正規化なし）、RRC設計はradix-2 IFFT（1/N正規化）に変更。
  フィルタ形状、窓、ゲインは維持しますが、浮動小数点の丸め順序は異なります。
- GCC `target_clones`はWindowsで無効化。coreの依存の代わりに`cf32`の型定義だけを供給。
- 入力はlibrtlsdrまたはcf32_le。出力フレーミングは旧std-t98-toolsの初期形式。
- 同期・CRCによる捕捉、tentative、flywheel、喪失の判定はSPEARのまま。電力スケルチでDSPや出力を止めません。

公開リポジトリに実録音は含めていません。RF感度や受信品質は実機で別途検証が必要です。
GPL-3.0のライセンス本文はLICENSE。
