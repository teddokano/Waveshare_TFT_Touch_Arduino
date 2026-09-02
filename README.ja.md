# Waveshare_TFT_Touch_Arduino

[English](README.md) | 日本語

[![Compile examples](https://github.com/teddokano/Waveshare_TFT_Touch_Arduino/actions/workflows/compile-examples.yml/badge.svg)](https://github.com/teddokano/Waveshare_TFT_Touch_Arduino/actions/workflows/compile-examples.yml)

[Waveshare 2.8inch TFT Touch Shield](https://www.waveshare.com/2.8inch-tft-touch-shield.htm)(SKU: 10684、Rev2.1: ST7789V LCD + XPT2046 抵抗膜タッチ)用のArduinoドライバです。Arduino Uno R3のシールド形状。

![TouchPaint](img/TouchPaint.jpg)
*examples/TouchPaint を FRDM-MCXA153 で実行*

## 概要

このライブラリが提供するもの:

- **`ST7789`** — 描画プリミティブ(塗りつぶし/点/線/矩形/円)に加え、任意のピクセルデータ(デコード済み画像など)を240x320のST7789V LCDへ流し込むための `startWrite()` / `writePixels()` / `endWrite()`
- **`XPT2046`** — XPT2046タッチコントローラからの、生の座標および較正済み座標の読み取り

両者はシールド上のmicroSDスロットと1本のハードウェアSPIバスを共有します(チップセレクトは個別)。ピン割り当てはシールドのPCBで固定されているため、配線も設定も不要です。SDカードアクセスを混在させる場合(`SDBitmapViewer`参照)は、各デバイスの `SPI.beginTransaction()` / `endTransaction()` の対を短く保ち、重ならないようにしてください。`ST7789::startWrite()` のトランザクションを開いたままSDの読み出しを行ってはいけません。

```cpp
#include <SPI.h>
#include <ST7789.h>
#include <XPT2046.h>

ST7789  tft(D10, D7, D9);   // LCD_CS, LCD_DC, LCD_BL
XPT2046 touch(D4, D3);      // TP_CS, TP_IRQ

void setup() {
  tft.begin();
  tft.setRotation(1);       // 横向き、320x240
  touch.begin();
  touch.setCalibration(0, 4095, 0, 4095, true, true, false);

  tft.fillScreen(ST7789_BLACK);
  tft.fillCircle(160, 120, 40, ST7789_RED);
}

void loop() {
  uint16_t x, y;
  if (touch.getPoint(x, y, tft.width(), tft.height())) {
    tft.fillCircle(x, y, 2, ST7789_WHITE);
  }
}
```

## 対応デバイス

型番|ヘッダファイル|インターフェース|備考
---|---|---|---
[ST7789V](https://www.waveshare.com/2.8inch-tft-touch-shield.htm)|`ST7789.h`|SPI (モード0、最大24MHz)|240x320 LCDコントローラ
[XPT2046](https://www.waveshare.com/2.8inch-tft-touch-shield.htm)|`XPT2046.h`|SPI (モード0、最大約2MHz)|4線式抵抗膜タッチコントローラ、LCDとSPIバスを共有

`ST7789.h` では、チャンネルあたり8ビットの成分からRGB565の色値を作る `rgb565(r, g, b)` も提供しています(`SDBitmapViewer` がBMPの24ビットピクセルの変換に使用)。

## 導入

このリポジトリをArduinoの `libraries/` フォルダにコピー(または `git clone`)し、Arduino IDEを再起動してください。

標準のArduino API(`pinMode` / `digitalWrite` / `SPI`)のみを使用しており、ボード固有の依存はありません。

> **注:** `D10` / `D7` などのピン名は、一部の32ビットコア(このシールドで使うmcx-arduino-core、UNO R4 Minimaのrenesas_uno)でしか定義されていません。実際のUNO R3が使うAVRコアには存在しません。`<ST7789.h>` / `<XPT2046.h>` は [`src/PinCompat.h`](src/PinCompat.h) を読み込み、AVRコアの場合に限って `D0`〜`D13` を補います。判定に `#ifndef D0` ではなくコンパイラマクロ `__AVR__` を使っているのは、既に定義しているコアではD0がプリプロセッサマクロではなくenumのメンバだからです。これにより `D10` 形式のピン名がどの環境でも使えます。

### ビルド状況

push のたびに [`.github/workflows/compile-examples.yml`](.github/workflows/compile-examples.yml) で全サンプルがコンパイルされます(このファイル冒頭のバッジ参照)。これが示すのはあくまで*コンパイルが通ること*であり、実機で動作したことではありません。移植可能な4つのサンプルは下記4ボード全てでビルドされ、いずれも警告ゼロ(`arduino-cli --warnings all`)です。`SDBitmapViewerDemo` は対応する2ボードでのみビルドされます(他のボードでは意図的に `#error` で停止するため)。

ボード|コア|コンパイル (CI)|実機動作確認
---|---|---|---
FRDM-MCXN947|[mcx-arduino-core](https://github.com/teddokano/mcx-arduino-core)|可|`TouchPaint`、`GraphicsPrimitivesDemo`、`TouchCalibration`、`SDBitmapViewer` の4つとも確認済み
FRDM-MCXA153|[mcx-arduino-core](https://github.com/teddokano/mcx-arduino-core)|可|`TouchPaint`(LCD・タッチとも動作確認済み。ピクセル一括転送の修正により、塗りつぶし/描画速度はUNO R3・R4 Minimaと同等)、`SDBitmapViewer`
UNO R4 Minima|`arduino:renesas_uno`|可|`TouchPaint`(LCD・タッチとも動作確認済み。30秒以上電源を切った真のコールドブートを含む)、`GraphicsPrimitivesDemo`、`TouchCalibration`、`SDBitmapViewer`
UNO R3|`arduino:avr`|可|`TouchPaint`(同上)、`GraphicsPrimitivesDemo`(4方向の回転すべて確認)、`TouchCalibration`(画面端まで正確に追従することを確認)、`SDBitmapViewer`(SDHC/FAT32カードで、一覧・描画・タッチ送りすべて確認)

nxp:mcxボードで `SDBitmapViewer` を動かすには、mcx-arduino-core側の修正が2つ必要でした。コアが定義していなかった `MOSI` / `MISO` / `SCK` のピンマクロ([mcx-arduino-core#1](https://github.com/teddokano/mcx-arduino-core/issues/1))と、`Print` クラスに欠けていた `setWriteError()` / `getWriteError()` / `clearWriteError()`([mcx-arduino-core#3](https://github.com/teddokano/mcx-arduino-core/issues/3))です。どちらも公開済みの [`0.3.0`](https://github.com/teddokano/mcx-arduino-core/releases/tag/0.3.0) に含まれており、CIでもFRDM-MCXA153・FRDM-MCXN947の両方で他のサンプルと同様に `SDBitmapViewer` をビルドしています。

同じ `0.3.0` では、以前ここに既知の問題として記録していたFRDM-MCXA153の `TouchPaint` の黒点アーティファクトも解消しました。[mcx-arduino-core#2](https://github.com/teddokano/mcx-arduino-core/issues/2) の修正(デバイスを切り替えるたびに `SPI.beginTransaction()` でLPSPIを完全に再初期化するのをやめ、その場での軽い再設定に変更)によるものです。当時このライブラリ側で両デバイスのSPISettingsを同一に固定して試した際には原因を特定できていませんでしたが、FRDM-MCXA153・FRDM-MCXN947の実機で解消を確認しました。

さらにその「軽い再設定」自体が、実はハードウェアに一度も届いていなかったことが後に判明しました。要求したSPIクロックは黙って捨てられ、バスは初期化時の設定のままだったのです([mcx-arduino-core#4](https://github.com/teddokano/mcx-arduino-core/issues/4)、[`0.4.1`](https://github.com/teddokano/mcx-arduino-core/releases/tag/0.4.1) で修正)。これらのボードでは `0.4.1` 以降を使ってください。症状については「トラブルシューティング」を参照してください。

![SDBitmapViewer](img/le_petit_prince.jpg)
*examples/SDBitmapViewer を FRDM-MCXN947 で実行*

## 中身

### サンプル

スケッチ|内容
---|---
`TouchPaint`|赤/緑/青/灰のコーナーテストパターンを描画し(LCDの向きとRGB順序が一目で確認できる)、続いて画面座標をシリアルへ出力する簡単なタッチペイントを実行
`SDBitmapViewer`|シールドのmicroSDスロットにある24ビット無圧縮BMPを一覧し、LCDに表示。画面のどこかをタッチすると次の画像へ。標準の `SD` ライブラリ(Arduino IDE同梱)が必要。macOS(Finderや `cp`)がカードへのコピー時に作る `._なんとか.bmp` というメタデータファイルは自動で除外します。カードのルートに `/PLAYLIST.JSN`(BMPパスのJSON配列、例: `["/PICS/SUNSET.BMP", "/LOGO.BMP"]`)があれば、そこに書かれたファイルを書かれた順に表示します(サブフォルダ可)。無ければルート直下の `.bmp` を全て、FATのディレクトリ順で表示します。拡張子が `.JSON` ではなく `.JSN` なのは、同梱のSDライブラリが8.3形式のファイル名しか扱えず、4文字の拡張子では開くことすらできないためです(中身は普通のJSONです)
`GraphicsPrimitivesDemo`|すべての描画プリミティブ(fillRect/drawRect/線/円)を一通り実行し、数秒ごとに `setRotation()` の4方向を巡回。立ち上げ確認と目視でのリグレッション確認向け、LCDのみ使用
`SDBitmapViewerDemo`|同じ発想をさらに進めたもので、**FRDM-MCXA153とFRDM-MCXN947専用**。タップ/スワイプ/長押しでの操作、スクリーンセーバー、画像切り替え時の方向付きワイプ、SDカードへのログを備え、これらすべてをカード上の1つのJSONファイルで設定します。[専用のREADME](examples/SDBitmapViewerDemo/README.ja.md)を参照してください。他のボードでは `#error` で停止するので、最初に触るサンプルとしては上記の `SDBitmapViewer` が適しています
`TouchCalibration`|画面に表示される4つの十字をタッチしていくと、そのまま貼り付けられる `setCalibration()` の呼び出しを出力するウィザード。TouchPaintのような決め打ちではなく、軸の入れ替えや反転を自動判定します

ライブラリのインストール後: `ファイル` → `スケッチ例` → `Waveshare_TFT_Touch` からスケッチを選択

## ピン割り当て

シールドのPCB(Arduino Uno R3ヘッダ)で固定されており、変更できません。

信号|ピン
---|---
LCD_CS|D10
LCD_DC|D7
LCD_BL|D9
TP_CS|D4
TP_IRQ|D3
SD_CS|D5(オンボードmicroSDスロット。`SDBitmapViewer` 参照)
SCLK / MOSI / MISO|D13 / D11 / D12(ハードウェアSPI。LCD・タッチ・SDで共有)

ヘッダにLCDのリセットピンは出ていません。`ST7789::begin()` はパワーオンリセット直後の状態から直接パネルを初期化します。

## タッチの較正

`XPT2046::setCalibration()` は、12ビットの生ADC値を画面ピクセルへどう対応付けるかを決めます。サンプルの既定値(両軸とも `0..4095` の生値範囲、`swapXY=true, invertX=true, invertY=false`)は較正なしの全範囲を使っていますが、入れ替え・反転フラグ自体はこの回転方向に対して正しいことが実機で確認されています。同じシールドの [Zephyr移植](https://github.com/teddokano/zephyr-waveshare-2.8-tft-touch-shield) にも、画面四隅をタッチして得た同じ知見が記録されています。生のXチャンネルはパネルの垂直軸に正しく対応し(上が小、下が大)、生のYチャンネルは水平軸に対応するが反転している(左が大、右が小)、というものです。

お手元のパネルの生値範囲が `0..4095` 付近まで届かない場合は、`TouchCalibration` サンプルを実行してください。その個体に合わせた `setCalibration()` の呼び出しがそのまま貼り付けられる形で出力されます。

## トラブルシューティング

**配線もソフトウェアも正しいのに、SPIバス上の何も応答しない(LCDは暗いまま、タッチも反応しない):** 少なくとも一部のシールドでは、基板裏のはんだジャンパ **SB1/SB2/SB3**(回路図上のR34/R35/R36付近)が未接続のまま出荷されており、SPIバスの一部が物理的に開放されています。ソフトウェアを疑う前に、はんだを盛るか0Ωの抵抗でブリッジしてください。これは同じシールドをZephyrで立ち上げた際に苦労して見つけた実際のハードウェア不良で、Arduino固有の話ではないため、このライブラリのサンプルでも同様に起こります。

**FRDM-MCXA153で、LCD_CS(D10)への書き込みは成功するのにパネルが反応しない:** 同じZephyrでの立ち上げ時に判明したのですが、このボードの*既定の*SPIピンマルチプレクサはD10を通常のGPIOではなくLPSPIのハードウェアチップセレクト機能に割り当てます。この状態ではD10への `digitalWrite()` は物理ピンに一切影響しません(SPI転送もGPIO呼び出しもソフトウェア上は成功を返します)。mcx-arduino-coreでこの不具合には遭遇していませんが、LCD_CSが「効いていない」ように見える場合は、D10がGPIOではなくハードウェアSPI CSに割り当てられていないかボードのピン設定を確認してください。

**`XPT2046::getPoint(x, y, z, ...)` のZ(圧力)値は「どのくらい強く押されたか」の指標としては信頼できません:** 実機で測ると、Zは押す力だけでなく画面上の位置によっても大きく変化します。例えば左上隅では、強く押しても明らかに低い値になります。これを使って筆圧に応じた線の太さを変えるサンプルを作ったところ、その隅では押す強さに関わらず細い線しか描けませんでした。Zはタッチの有無の判定には有用ですが(`getRaw()` / `getPoint()` の内部でもその用途のみに使っています)、位置による差を補正せずに絶対値を利用するのは避けてください。

**mcx-arduino-coreが [`0.4.1`](https://github.com/teddokano/mcx-arduino-core/releases/tag/0.4.1) より古いnxp:mcxボードで、SPI全般が要求したクロックよりはるかに遅い(例: `SDBitmapViewer` で最初の画像が出るまで10秒かかる):** これはコア側の不具合で、このライブラリの問題ではありません。`0.4.1` で修正済みです([mcx-arduino-core#4](https://github.com/teddokano/mcx-arduino-core/issues/4))。`SPI::frequency()` がLPSPIを無効化した直後にSDKの `LPSPI_MasterSetBaudRate()` を呼びますが、この関数はガードとして有効ビットを読み直します。LPSPIは別クロックドメインにあるため書き込みがそれほど早くは観測できず、ガードは「まだ有効」と判断して分周器を設定せずに戻ります。結果として要求クロックは黙って捨てられ、バスは初期化時の設定のまま残ります。実測では、24MHzを要求した1バイト転送2000回がFRDM-MCXA153で302ms、FRDM-MCXN947で7.7秒かかっていたものが、修正後は2msになりました。スケッチ側で回避する方法はないので、コアを更新してください。

## 既知の問題

**LCDは動作するのに、SB1/SB2/SB3をブリッジしてもタッチが反応しない、または電源を入れ直すたびに動いたり動かなかったりする:** ソフトウェアを疑う前に、TP_IRQ / TP_CS のヘッダの接触を確認してください。実際のUNO R3で起きたのはまさにこれで、ヘッダソケットのほこりやピンの接触不良により、起動のたびに挙動が変わっていました。ソケットを清掃しピンを軽く曲げて確実に接触させたところ、コードを一切変更せずに解消しました。LCDは書き込み専用でTP_IRQ・MISO・TP_CSを一切使わないため、LCDは完全に正常に見えるのにタッチだけが間欠的に失敗します。再起動や抜き差しで挙動が変わる場合は、ドライバより先に物理的な接続を疑ってください。

## 謝辞

このライブラリはWaveshare 2.8inch TFT Touch Shieldそのもののハードウェアを対象としていますが、ドライバの*ロジック* — ST7789Vのレジスタ初期化の流れとXPT2046のSPI読み出しプロトコル — は、Waveshareのライセンス不明のサンプルコードではなく、[Zephyr Project](https://www.zephyrproject.org/) がこれらのチップ向けに公開している、ライセンスの明確な(Apache-2.0)ドライバを参考にしています。

- `src/ST7789.cpp` の初期化シーケンス構造: [`zephyr/drivers/display/display_st7789v.c`](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/display/display_st7789v.c) — Copyright (c) 2017 Jan Van Winkel, 2019 Nordic Semiconductor ASA, 2019 Marc Reilly, 2019 PHYTEC Messtechnik GmbH, 2020 Endian Technologies AB, 2022 Basalte bv, 2026 Abderrahmane JARMOUNI. SPDX-License-Identifier: Apache-2.0
- `src/XPT2046.cpp` の読み出しプロトコル: [`zephyr/drivers/input/input_xpt2046.c`](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/input/input_xpt2046.c) — Copyright (c) 2023 Seppo Takalo. SPDX-License-Identifier: Apache-2.0

その流れの中で送るパネル固有の調整値(ガンマ/VCOM/ポーチのパラメータバイト)は、Waveshareがこのパネル向けに公開している値であり、同社のSTM32 HAL参考コードと照合しています。この種のレジスタと値の組は、著作物としての表現ではなくパネルの事実上の設定データとして扱っています。このパネル向けの他のST7789Vドライバも、いずれも非常に近い数値に収束しています。

サンプルにある `swapXY` / `invertX` / `invertY` の既定値、および上記のSB1/SB2/SB3とFRDM-MCXA153のCSマルチプレクサに関する記述は、同じ著者による [このハードウェアのZephyrサポート](https://github.com/teddokano/zephyr-waveshare-2.8-tft-touch-shield) で得られた実機での知見に基づいています。これらはZephyr固有のコードではなくハードウェアの事実ですが、発見された場所として記載しています。

## ライセンス

MIT — [LICENSE](LICENSE) を参照してください。`src/ST7789.cpp` と `src/XPT2046.cpp` の一部は、Apache-2.0ライセンスのZephyr Projectのコードを参考に構成しています。上記「謝辞」を参照してください。
