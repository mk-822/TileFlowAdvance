# Tile Flow Advance

5レーン×20段のベルトを流れるタイルを受け止め、5×5のプールで同じ色を縦・横・斜めに3枚以上そろえる、Game Boy Advance向けのパズルゲームです。ブラウザ版 Tile Flow のルールをC++とButanoで移植しました。 [移植元](https://github.com/mk-822/tile-flow/tree/a6da752)

![Tile Flow Advanceのプレイ画面](docs/gameplay.png)

- 3つの難易度と、WAVE 1・6・11からのスタートに対応します。
- 各ウェーブの開始時に目標がスライド表示されます。WAVE 5・10・15…のクリア後は、次のウェーブか、そこから5・10ウェーブ先を選べます。
- 消去・連鎖中はベルトが止まり、パドルは操作できます。光っている縦列に同色を足すと、4枚・5枚をまとめて消せます。
- 難易度別のベストスコア、難易度、音量、ミュートをSRAMに保存します。

## 操作

| ボタン | 操作 |
| --- | --- |
| 十字キー ← / → | パドル移動（長押しで連続移動）、タイトルの選択値の変更、ウェーブスキップ選択 |
| 十字キー ↑ / ↓ | タイトル画面で難易度・開始ウェーブ・START の行を移動 |
| A | 一番上のタイルを落とす、メニューの決定。タイトル画面では次の行へ進み、START 行でゲーム開始 |
| B または ↑ | 一番上のタイルをベルトへ投げ返す |
| R または ↓ 長押し | ベルトとタイルの出現を3倍速にする |
| START | タイトル画面のどの行からでも開始。一時停止・再開、結果画面で決定 |
| SELECT | ミュート切り替え |
| L / R | タイトル・一時停止中に音量を調整 |
| A / B（ゲームオーバー時） | 同じ条件で再挑戦 / タイトルへ戻る |

## ビルド

devkitPro の GBA 開発環境（devkitARM、grit、mmutil、gbafix）と Python 3 が必要です。リポジトリのルートで、devkitPro のシェルから実行してください。

~~~sh
git clone --depth 1 --branch 21.3.0 --filter=blob:none --sparse https://github.com/GValiente/butano.git .deps/butano
git -C .deps/butano sparse-checkout set butano
make
~~~

Butano がすでにある場合は `make LIBBUTANO=/path/to/butano/butano` で `butano.mak` のあるディレクトリを指定できます。プロジェクト直下の `butano/` と、隣の `Hextris-Advance/butano/` も自動で検索します。Windows では `DEVKITPRO` と `DEVKITARM` を設定し、`python`、`grit`、`mmutil`、`gbafix` が PATH から実行できることを確認してください。

ビルドするとプロジェクト直下に `TileFlowAdvance.gba` ができます。GBAエミュレータまたはフラッシュカートで開けます。中間ファイルとビルド結果を削除するには `make clean` を実行します。

効果音はブラウザ版の合成音をもとに、ビルド時に `build/sfx/` にGBA用のモノラルWAVとして生成します。 [効果音の生成コード](tools/generate_audio.py)

### グラフィックの再生成

ビルドには `graphics/` の画像を使います。描画を変更する場合は、Pillow をインストールして `python tools/generate_graphics.py` を実行してください。

## ライセンスとクレジット

ゲームのソースコード、オリジナルのグラフィック、ドキュメントはMITライセンスです。 [LICENSE](LICENSE)

BGM 15曲は Hextris-Advance から流用しています。 [BGMクレジット](AUDIO_CREDITS.md)

Butano、Maxmod、ツールチェーンなどのライセンスは各コンポーネントに適用されます。 [依存関係のライセンス](licenses/README.md)
