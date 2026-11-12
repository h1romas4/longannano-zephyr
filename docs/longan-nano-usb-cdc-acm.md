# Longan Nano の USB CDC ACM 調査メモ

このメモは、Longan Nano の USB FS ペリフェラルと、このプロジェクトで USB CDC ACM を使う場合の Zephyr 4.5.0 の状況をまとめたものです。現時点では、動作未確認の値を使った USB overlay は追加していません。

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
- RIOT の GD32VF103 ヘッダーでは `USBFS_IRQn` は 86 です。Zephyr overlay に設定する割り込み番号・優先度は、Zephyr の ECLIC 定義との照合が必要です。
- RIOT の初期化コードでは、USB 用 48 MHz クロックの設定、USBFS クロックの有効化と周辺リセット、割り込み設定を行っています。

参考: [RIOT GD32V USB 設定](https://github.com/RIOT-OS/RIOT/blob/master/boards/common/gd32v/include/cfg_usbdev_default.h)、[RIOT GD32V USB 初期化](https://github.com/RIOT-OS/RIOT/blob/master/pkg/tinyusb/hw/hw_gd32v.c)、[GD32VF103 割り込み定義](https://github.com/RIOT-OS/RIOT/blob/master/cpu/gd32v/include/vendor/gd32vf103.h)。

`GHWCFG` と FIFO の値は Zephyr binding で静的に要求されますが、調査した Zephyr/RIOT のボード設定や公開資料から、GD32VF103 用の値を確定できていません。値を推測してノードを有効化すると、DWC2 ドライバの初期化・FIFO 設定が誤る可能性があるため、overlay にはまだ記載していません。

## 次の検証

実機を用意できたら、USBFS のクロックとリセットを正しく設定した診断コードから、`GHWCFG1/2/4` と FIFO の初期値を取得します。その値と Zephyr の ECLIC/クロック設定を照合して overlay を作成し、まず DWC2 UDC の初期化と USB 列挙を確認します。その後、USB CDC ACM クラスを有効にして、ホスト側で仮想 COM ポートとして通信できるかを検証します。
