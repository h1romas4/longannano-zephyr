# Longan Nano の USB CDC ACM 調査メモ

このメモは、Longan Nano の USB FS ペリフェラルと、Zephyr 4.5.0 で USB CDC ACM を動かすための調査・実験結果をまとめたものです。プロジェクト内の実験用 UDC と overlay を使い、Zephyr の CDC ACM サンプルで実機通信を確認しました。

## ハードウェア

Longan Nano の GD32VF103CBT6 には USB FS (OTG) ペリフェラルが内蔵されています。基板の USB Type-C コネクタのデータ線は MCU の USB D− / D+ に接続され、RIOT のボード資料では PA11 が D−、PA12 が D+ と記載されています。USB-UART 変換チップ経由の接続ではありません。

- [Sipeed Longan Nano の製品・ハードウェア概要](https://longan.sipeed.com/en/)
- [RIOT の Longan Nano ボード資料](https://github.com/RIOT-OS/RIOT/blob/master/boards/sipeed-longan-nano/doc.md) — USB ピン割り当て、USB FS OTG、DFU、CDC ACM の説明
- [Longan Nano 回路図](https://dl.sipeed.com/fileList/LONGAN/Nano/HDK/Longan%20Nano%203302/2_Schematic/Longan_nano_3302_Schematic.pdf)

USB は起動モードによって用途が異なります。ROM DFU ブートローダーでの書き込みと、アプリケーション起動後に CDC ACM デバイスとして列挙することは別の機能です。RIOT のボード資料では、標準入出力に USB CDC ACM を使用し、Linux では `/dev/ttyACM<n>` として認識される構成が説明されています。

## このプロジェクトの現状

このプロジェクトのコンソールは USART0 です。Zephyr の Longan Nano ボード定義では `zephyr,console` と `zephyr,shell-uart` が USART0 を指し、README では TX: PA9、RX: PA10、115200 baud と案内しています。USB DFU による書き込みはできますが、通常起動後の USB CDC ACM は設定されていません。

- [Zephyr 4.5.0 Longan Nano 共通 Devicetree](https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/boards/sipeed/longan_nano/longan_nano-common.dtsi)
- [プロジェクト README](../README.md)

Zephyr 4.5.0 には汎用の [`snps,dwc2` UDC ドライバ](https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/drivers/usb/udc/udc_dwc2.c)があります。ドライバの有効化には [`UDC_DWC2` の Kconfig](https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/drivers/usb/udc/Kconfig.dwc2) が対応 Devicetree ノードを要求しますが、同バージョンの Longan Nano ボード定義には DWC2 ノードがありません。したがって、汎用ドライバがあるだけでは、このボードで USB CDC ACM がそのまま動く状態にはなりません。

## overlay に必要な情報

Zephyr 4.5.0 の [`snps,dwc2` binding](https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/dts/bindings/usb/snps%2Cdwc2.yaml) は、`reg`、`interrupts`、エンドポイント数に加えて、次のハードウェア構成値を必須にしています。

- `ghwcfg1`、`ghwcfg2`、`ghwcfg4`
- `g-rx-fifo-size`、`g-np-tx-fifo-size`、`g-tx-fifo-size`

調査で得られた既知情報は次のとおりです。

- RIOT の GD32V USB 定義では、USB FS ペリフェラルのベースアドレスは `0x50000000`、内蔵 PHY を使用し、USBFS の割り込みとして `USBFS_IRQn` を指定しています。
- RIOT の GD32VF103 ヘッダーでは `USBFS_IRQn` は 86 です。生成された Zephyr の ISR テーブルでも割り込み 86 への登録を確認しました。
- RIOT は GD32VF103 の USBFS を Synopsys DWC2 としていますが、[GD32V 用レジスタ定義](https://github.com/RIOT-OS/RIOT/blob/master/cpu/gd32v/include/vendor/usbdev_gd32v.h)は汎用 DWC2 と異なります。`GCCFG` は `0x38`、`CID` は `0x3C`、`0x40..0x48` は予約、`GHWCFG3` は `0x4C`、`0x50` は予約です。そのため、汎用 DWC2 の `GSNPSID`、`GHWCFG1/2/4` に相当する位置を読んだ結果が 0 でも、ハードウェア構成が 0 とは限りません。
- RIOT の定義では FS の IN エンドポイント数は EP0 を含めて 4、RX FIFO の既定値は 128 words、FIFO 総量は **1280 bytes（320 words）**です。TinyUSB の GD32VF103 向け定義も FIFO 深さを 320 words としています。
- RIOT の初期化コードでは、USB 用 48 MHz クロックの設定、USBFS クロックの有効化と周辺リセット、`GCCFG` の PHY/VBUS 設定、割り込み設定を行っています。

参考: [RIOT GD32V USB 設定](https://github.com/RIOT-OS/RIOT/blob/master/boards/common/gd32v/include/cfg_usbdev_default.h)、[RIOT GD32V USB 初期化](https://github.com/RIOT-OS/RIOT/blob/master/pkg/tinyusb/hw/hw_gd32v.c)、[TinyUSB GD32 DWC2 定義](https://github.com/hathach/tinyusb/blob/master/src/portable/synopsys/dwc2/dwc2_gd32.h)、[GD32VF103 割り込み定義](https://github.com/RIOT-OS/RIOT/blob/master/cpu/gd32v/include/vendor/gd32vf103.h)。

この差異のため、Zephyr の汎用 DWC2 ドライバに overlay だけを追加しても適合しません。実験用ドライバは、読み取れない capability レジスタの代わりに論理的なエンドポイント能力を使用します。これらは実機から読み出したレジスタ値ではありません。

## 実機でのレジスタ取得

通常アプリを変更せず、診断用のビルドディレクトリを分けて作成します。

```sh
west build -b longan_nano -d build/west/usbfs-info . -- \
  -DLONGAN_NANO_USBFS_INFO=ON
west flash -d build/west/usbfs-info --runner dfu-util
```

診断プログラムは USBFS AHB クロックを有効にして USBFS をリセットし、RIOT の GD32V レジスタ配置に合わせて `GOTGCTL`、`GUSBCFG`、`GCCFG`、`CID`、`GHWCFG3`、`GRXFSIZ`、`GNPTXFSIZ`、および `DIEPTXF1..3` を読み取ります。LCD にはコア設定・クロック状態・送信 FIFO 値を表示し、BOOT0 ボタンを押して離すと次ページへ進みます。ページ待機中は赤色 LED が点滅します。画面下端の `BOOT0 0/1` はボタン入力の現在値です。`CLOCK STATE` ページには起動時に保存した前回リセット原因フラグも表示します。USART0 にも全値を出力します。

この診断では USB データ通信を開始せず、PLL や USBFS クロック分周器も変更しません。実機で取得したレジスタ値は次のとおりです。

| Register | Value |
|---|---:|
| `GOTGCTL` | `0x000D0800` |
| `GUSBCFG` | `0x00000A80` |
| `GCCFG` | `0x00290000` |
| `CID` | `0x00001000` |
| `GHWCFG3` | `0x00000000` |
| `GRXFSIZ` | `0x00000200` |
| `GNPTXFSIZ` | `0x02000200` |
| `DIEPTXF1` | `0x02000400` |
| `DIEPTXF2` | `0x02000600` |
| `DIEPTXF3` | `0x02000800` |

`GCCFG=0x00290000` は診断プログラムが設定した PHY power-on、VBUS sensing disable、B-session valid comparator enable の各ビットが読み戻せている値です。FIFO size レジスタは上位16 bit が depth、下位16 bit が開始 word offset を表すため、取得値は RX FIFO が offset 0 / 512 words、非周期 IN FIFO が 512 / 512 words、IN endpoint 1..3 がそれぞれ 1024、1536、2048 から 512 words です。単純合算では 2560 words となり、RIOT と TinyUSB の定義にある総量 320 words と一致しません。値は USBFS リセット直後の読み取り結果として記録し、レジスタの初期値と利用可能な容量の関係を確認するまでは Zephyr の FIFO 配置として採用しません。`GHWCFG3=0` も取得値として記録し、DWC2 の汎用レジスタ定義をこの IP に適用できる根拠とはみなしません。

他に `RCU_CFG0=0x202B040A`、`RCU_AHBEN=0x00001014`、`RCU_AHBRST=0`、`RCU_RSTSCK=0x04000000` を確認しました。`USB DIV=0` は PLL/1.5 を選択します。Zephyr の GD32VF103 初期化は PLL を 108 MHz に設定するため、この状態の USBFS クロックは 72 MHz となり、USB に必要な 48 MHz ではありません。`RCU_RSTSCK` の `EPRSTF` は前回リセットが外部リセット端子によるものだったことを示します。

GD32VF103 HAL の USB 分周器は PLL/1、/1.5、/2、/2.5 を選べるため、108 MHz PLL から 48 MHz は生成できません。HAL にある 96 MHz PLL 設定と USB の PLL/2 を検証しました。Zephyr の Devicetree の CPU 周波数とタイマの timebase も合わせています。

96 MHz PLL の検証には、通常の診断とは別のビルドを使用します。

```sh
west build -b longan_nano -d build/west/usb-clock-test . -- \
  -DLONGAN_NANO_USBFS_INFO=ON -DLONGAN_NANO_USB_CLOCK_TEST=ON
west flash -d build/west/usb-clock-test --runner dfu-util
```

この検証用ビルドは `SystemInit` をラップし、起動初期に PLL を 108 MHz から 96 MHz に切り替えて USB 分周器を PLL/2 に設定します。Devicetree の CPU 周波数と timebase もそれぞれ 96 MHz、24 MHz に変更します。PLL 切り替え失敗時や設定が一致しない場合は停止します。起動後に BOOT0 を押して離すと `USB 48M TEST` ページに進み、`USB DIV 3` と `RCU CFG0` を確認できます。これは設定の読み戻しであり、USB 信号の周波数測定や列挙成功を意味するものではありません。通常の診断とアプリのクロック設定は変更しません。

実機でこのビルドを起動し、`RCU CFG0=0x20DF040A`、`USB DIV 3` および赤色 LED の点滅を確認しました。`RCU CFG0` はシステムクロック源が PLL、PLL 倍率が 24、USB 分周器が PLL/2 であることを示します。HAL の 4 MHz PLL 入力に対して、設定上は PLL 96 MHz、USB 48 MHz になります。LCD 表示とボタン操作も確認できました。

Zephyr の DWC2 UDC は初期化時にも `GHWCFG1..4` を読み、`GHWCFG4` の専用 TX FIFO フラグがない場合は `-ENOTSUP` を返します。GD32V の予約領域に対して Devicetree の値を用意するだけではこの実行時判定を通せません。FIFO 総量やエンドポイント能力も含めた GD32 固有の扱いが必要です。

## 実験用 UDC と実機検証

Zephyr サブモジュールは変更せず、[プロジェクト内のモジュール](../drivers/usb_gd32/)に Zephyr 4.5.0 の DWC2 UDC を複製・改変しました。元の Apache-2.0 ライセンス表示を保持しています。Zephyr の内部ヘッダーに依存するため、Zephyr 更新時には上流の変更と互換性を確認する必要があります。ビルド・書き込み方法は [README](../README.md#usb-cdc-acm-implementation-status) を参照してください。

- 診断と同じ早期起動処理で PLL を 96 MHz、USB を PLL/2 に設定します。CPU/timebase は overlay で 96/24 MHz とし、ビルド時に検証します。これは汎用的なクロックドライバ対応ではありません。
- FIFO は RIOT と TinyUSB の 320 words を上限とし、RX に 128 words、4 つの TX FIFO に各 32 words を割り当てます。リセット時の FIFO レジスタ値との不一致は未解決です。
- GD32 の IN endpoint NAK-effective 状態はレベル動作であり、通常の IN 割り込みマスクに含めると EP0 の割り込みが消えず列挙が停止しました。マスクから除外し、IN endpoint の無効化時は状態を期限付きでポーリングします。
- Longan Nano 実機で Zephyr の CDC ACM echo サンプルが Linux の `/dev/ttyACM0` に列挙し、1、16、63、64、65、128、512、1024 bytes のバイナリ echo を確認しました。繰り返しオープンして 4096 bytes を 128-byte 単位で送受信し、抜き差し後の再列挙と通信も確認しました。最後の endpoint 無効化処理変更後も 1〜1024 bytes の echo を再確認しました。
- Linux USBFS から data interface の bulk IN `0x82` と bulk OUT `0x01` に対して HALT の設定・解除を行い、各操作後の `GET_STATUS` を確認しました。その後 USB リセットと CDC ドライバの再接続を行い、1〜1024 bytes の echo に成功しました。HALT の時点で実際に転送がアクティブだったかは確認できていません。
- 繰り返し送受信中に USBFS リセットを行ったところ、CDC 接続が切れて再接続されましたが、最初の 256-byte echo は 64 bytes しか受信できませんでした。その後の 1〜256 bytes の echo は成功しました。さらに通信中リセットを 3 回行い、ポート再オープン後 300 ms 待ってからの各 256-byte echo は成功しました。最初の短い受信の原因は未調査であり、転送中リセットからの回復を保証するものではありません。テストには上記の実機 echo と同じファームウェアを使用し、最後の診断 LED・isochronous エラー処理変更はまだ実機に書き込んでいません。
- アクティブな転送のキャンセル・HALT、デバイス無効化・再有効化は未検証です。isochronous endpoint は公開していません。汎用 UDC としての動作を保証するものではありません。
- LCD 診断なしの標準サンプルは Lite の 64 KB flash を超えます。minimal logging の Lite ビルドは ROM 57,896 bytes、RAM 16,248 bytes で成功しましたが、Lite 実機では未確認です。ログは USART0 に出力されます。

サンプルの VID/PID は Zephyr のサンプル用です。通常アプリは引き続き USART0 を使用し、CDC ACM は有効化していません。アプリに組み込む際には、Zephyr Project 外の製品で Zephyr の VID を使用せず、適切な VID/PID を用意してください。
