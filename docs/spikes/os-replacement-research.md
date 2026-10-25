# Kindle Paperwhite 2 の OS 置き換え調査（完全置き換え / 半分置き換え）

- 調査日: 2026-10-23
- 対象: `ssh kindle` の個体（Paperwhite 2 / Pinot / Wario ボード、FW 5.12.2.1.1、Kernel 3.0.35-lab126）
- 方法: 端末は**読み取りのみ**（`uname`、`/proc`、`/sys`、`lsmod`、`modinfo`、`strings`、`find`）。`mntroot rw`、書き込み、再起動、upstart の start/stop、`fw_setenv`、`dd`、プロセスへの kill は一切していない。外部情報は公開 URL のみ。
- 表記: **【確認】** = 実機出力または一次情報で確認できた。**【推測】** = 確認事実からの推定、または未検証。
- 目標機能: C による `/dev/fb0` 自作ホーム、KOReader、Web ブラウザ、Wi-Fi、スリープ/復帰、フロントライト、充電表示。

---

## 0. 結論

### (1) 完全置き換え（mainline Linux / postmarketOS 等）: 現時点の PW2 では非現実的

**結論: 動く見込みが立っていない。着手するなら「研究プロジェクト」として 1〜2 人年規模の覚悟が要る。**

理由を重要な順に:

1. **PW2 向けの mainline / postmarketOS ポートが存在しない。**
   - postmarketOS（2026 年時点でパッケージ配布は `nura.eco` ドメイン。同一プロジェクトかは後述の未確認点）のデバイス一覧で Kindle は **Amazon Kindle 4 (amazon-yoshi) だけ**。PW1/PW2/PW3/Basic のページは見つからない。【確認（検索範囲）】
   - その Kindle 4 ページも Category=testing、prebuilt images=no、**Display/WiFi/Battery/USB Networking が Broken**、mainline だけ Works、という状態（2024-05-25 の Wayback スナップショット）。【確認】
   - PW2 を postmarketOS にしたい、という MobileRead の投稿は 2024-05 に 1 件あるが**返信ゼロ**。【確認】
2. **EPDC（電子ペーパーコントローラ）の mainline ドライバが無い。**
   - mainline の `imx6sl.dtsi` には `epdc: epdc@20f4000` ノードがあるが、**`compatible` プロパティが無い**（＝どのドライバにもバインドしない）。【確認】
   - `drivers/gpu/drm/imx/` は dc / dcss / ipuv3 / lcdc のみ、`drivers/video/fbdev/Makefile` にも EPDC は無い。【確認】
   - Andreas Kemnade 氏の out-of-tree `mxc-epdc`（DRM）が存在し、**i.MX6SLL の Kobo Libra H2O で「display works」**。RFC では **i.MX6SL の EPDC も対象**と明言されている。ただし PW2 のパネルでの実績は無い。【確認（本人報告・メーリングリスト）】
3. **最大の壁は EPDC ではなく MAX77696。**
   - PW2 の表示電源（`DISP_GATED`）、VCOM、タッチ電源（`TOUCH_VDDD`/`TOUCH_VDDA`）、フロントライト、充電、燃料計、RTC、電源キーは**すべて MAX77696 配下**にある（Amazon のボードファイルと実機 sysfs で確認）。【確認】
   - mainline には **MAX77696 のドライバが MFD / regulator / power_supply / rtc / leds / input(misc) の主要サブシステムに無い**（各 Makefile を確認）。【確認】
   - 同じ電子ペーパー機の Kobo は SY7636A や TPS65185 を使っており、**この 2 つは mainline に regulator ドライバがある**。Kindle だけ部品が違う。【確認】
4. **タッチの cyttsp4 は mainline から削除済み。** v5.15 の `drivers/input/touchscreen/Makefile` には存在するが、現行 master には無い。DT 対応も無く（i2c_device_id のみ）、コントローラファーム `pinot_0108.bin` のロードも必要。【確認】
5. **RAM 249MB、`/` の空き 31.6MB。** pmOS/Alpine の GUI 系は 512MB 前提（`alpine_kindle` の README が「at least 512MiB RAM」）。PW2 ではユーザーランドの選択肢も狭い。【確認】

**工数の目安【推測】**: 経験のあるカーネル開発者が、Kemnade 氏の EPDC ドライバと Amazon の GPL ソースを流用できる前提で、

- serial + eMMC + 画面 + タッチの PoC: **2〜4 人月**（画面が出るまでの不確実性込みで +2 人月の余裕を見る）
- Wi-Fi、サスペンド/復帰、充電/残量、フロントライトまで日常使用: **さらに 4〜8 人月**
- 合計 **6〜12 人月**。しかも「動く保証」は無い。

### (2) 半分置き換え（Amazon カーネル + 自作ユーザーランド）: 現実的。既に一部動いている

**結論: こちらが本命。純正 UI を起動せず、自作 C の fb0 ホーム + KOReader + 最小ブラウザ、という構成は十分現実的。**

根拠:

- 本機で **KOReader v2026.07.1 の「no framework」モード**が動いている。`koreader.sh --framework_stop` は `/etc/init.d/framework stop` または `stop lab126_gui` を実行する（実機のスクリプトで確認）。【確認】
- 本リポジトリの **khome が実際に起動中**（調査時点で pid 26329、RSS 23MB）。Amazon の UI（cvm/X）と共存して動いている。【確認】
- `powerd` は `lab126` 配下（`powerd.conf: start on started lab126`）なので、**`lab126_gui` を止めても残る**。つまりスリープ/復帰、フロントライト、残量、Wi-Fi（`wifid`）は純正ドライバのまま使える。これが (2) の決定的な利点。【確認】
- 起動時から自作 UI にする方法は 3 段階ある:
  1. KUAL から起動（現状の khome / KOReader）。最も安全。
  2. `/mnt/us/DONT_START_FRAMEWORK` を置く。`/etc/upstart/framework.conf:11` がこのファイルを見て Java framework（cvm）を起動しない。**`/mnt/us` 上の 1 ファイルで可逆**。ただし X/awesome/blanket は残る。【確認】
  3. upstart ジョブを追加して `lab126_gui` と `x` を止め、`lab126`（powerd 等）と自作アプリだけ起動。`lab126_gui` を止めると `kb` / `pillow` / `kfxreader` / `kfxview` / `contentpackd` なども連鎖停止する（各 .conf の `start on started lab126_gui` を確認）。`/sbin/init` 自体の置き換えも技術的には可能（`/` は ext3 で `mntroot rw` で書ける）が、起動失敗時に diags 以外の戻り道が無くなるので推奨しない。【確認 / 推測】
- ブラウザは **NetSurf の framebuffer 版**（`netsurf-fb -w 600 -h 800 -b 8`）が Kindle（PW3）で動いた報告がある。X 不要・8bpp のまま動く。Chromium は 256MB では現実的でない。【確認（PW3 の報告）/ 推測（PW2 の RAM 不足）】

**工数の目安【推測】**: 起動時から自作 UI + 最小デーモン構成で **1〜3 週間**。ブラウザ（NetSurf のビルド/操作系）と運用の堅牢化を足して **+1〜2 か月**。各段階で戻せる。

### 推奨

**(2) を段階的に進める。** (1) は「カーネルを新しくしたい」という目的なら魅力的だが、現状の PW2 では、動いている EPDC / タッチ / PMIC / フロントライトをすべて失って、それを書き直す作業になる。得るもの（新しいカーネル、セキュリティ更新）に対して失うものと工数が大きすぎる。

---

## 1. 部品ごとの対応表

### 1.1 ハードウェア同定（実機で確認）

| 部品 | PW2 での実体 | 根拠 |
|---|---|---|
| SoC | NXP i.MX6SoloLite（Cortex-A9 1 コア、996/792/396MHz OPP） | `/proc/cpuinfo`: `Freescale i.MX 6SoloLite based Wario Board`、`CPU part 0xc09` |
| ボード / コードネーム | Wario / **Pinot**（型番 DP75SDI "PINOT_WFO" 256MB） | `/etc/version.txt`: `pinot_wario`、`/proc/cpuinfo`、barebox のボード一覧 |
| PMIC | **MAX77696A**（E-Reader PMIC） | barebox の Kindle 6/7 ページ、i2c `0-003c` の `max77696` |
| タッチ | **Cypress TrueTouch Gen4 (cyttsp4)**、I2C `1-0024`、MT protocol B | `/proc/bus/input/devices` の `cyttsp4_mt`、sysfs |
| タッチ FW | `pinot_0108.bin`（55,607 bytes）を udev でロード | `/lib/firmware/cyttsp4/`、`/etc/udev/rules.d/40-cyttsp4fw.rules` |
| Wi-Fi | **Atheros AR6003**（SDIO `0271:0301` = AR6003_01） | `/sys/bus/sdio/devices/mmc1:0001:1/uevent`、mainline `sdio_ids.h` |
| Wi-Fi FW/校正 | `/opt/ar6k/target/AR6003/hw2.1.1/bin/`（athwlan.bin, bdata.bin, otp.bin, patch）、`board_data_file=.../AR6003_wfo_calfile.bin` | 実機のファイルと module parameter |
| 燃料計 | **MAX77696 内蔵**（`max77696-battery`。CYCLE_COUNT / CHARGE_FULL / CHARGE_NOW / VOLTAGE_NOW / CURRENT_NOW） | `/sys/class/power_supply/max77696-battery/uevent` |
| 充電 | MAX77696（`max77696-charger`, `max77696-uic`, `max77696-eh`） | `/sys/class/power_supply/` |
| フロントライト | **MAX77696 の LED/バックライト**（`max77696-bl`, 0〜4095） | `/sys/class/backlight/max77696-bl/`（max 4095, 現在 14） |
| RTC | MAX77696（i2c `0-0068`, `max77696-rtc`） | `/sys/bus/i2c/devices/` |
| 電源キー | MAX77696 onkey（`/dev/input/event0`, KEY_POWER のみ） | `/proc/bus/input/devices` |
| パネル | **ED060XD4U1**（6 型、758×1024、212dpi、8bpp） | 波形ファイル名 `320_G135_AA5431_ED060XD4U1_TC.wbf`、cmdline `video=mxcepdcfb:E60,bpp=8,x_mem=3M`、`fbink -e` |
| 波形（waveform） | eMMC の**未パーティション領域**（offset `0x1C41000` = sector 57,864、約3.75MiB、p1 の直前まで）を loop mount して `/mnt/wfm` に FAT でマウントし、`waveform_to_use/*.wrf.gz` を EPDC にロード。`/var/local/eink/waveforms/` にキャッシュもあり | `/usr/sbin/wfm_mount`、`/etc/upstart/display`、`/lib/udev/bin/waveform.sh`、`/sys/block/mmcblk0/mmcblk0p1/start` |
| 照度/近接/ホール | max44009（i2c 2-004a）、wario_prox（2-000d）、wario_hall | `/sys/bus/i2c/devices/*/name`（目標には必須でない） |
| 未使用の宣言 | `zforce2`（i2c 1-0050）が宣言されているがドライバ未バインド、input ノードも無い | sysfs。**実タッチは cyttsp4** |
| ストレージ | SanDisk iNAND 4GB eMMC（`004GE0`）、boot0/boot1 あり。NAND ではない | `/sys/block/mmcblk0/device/name`、`/proc/partitions`、`/proc/mtd` 不在 |

補足（興味深い観察）:

- **`/chroot` は Amazon WAF のサンドボックス用**。`/chroot/usr/java`、`/chroot/var/local`、`/chroot/mnt/us` などが bind mount されている（`/proc/mounts`）。ユーザーが Debian/Alpine を入れるための既存 chroot ではない。【推測（マウント構成から）】
- 調査時点の `dmesg` は 460 行で、先頭から `rcm` / `kfxreader` / `kfxview` / `wifilocker` の `undefined instruction` が並ぶ（`wifilocker` は 13 回）。**起動時の EPDC/Wi-Fi/PMIC ログはリングバッファから流れて残っていない**。ハード同定は sysfs / modinfo / strings で行った。【確認】

### 1.2 mainline 対応表

| 部品 / 機能 | mainline | 内容 | 出典 |
|---|---|---|---|
| i.MX6SL SoC（clock, pinctrl, MMC, I2C, USB, cpufreq） | ✅ | `imx6sl.dtsi` が存在。CPU OPP、GPC、USDHC、I2C、USB など定義済み | https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/arm/boot/dts/nxp/imx/imx6sl.dtsi |
| PW2 ボード DTS（Wario/Pinot） | ❌ | `Documentation/devicetree/bindings/arm/fsl.yaml` に `wario` / `pinot` / `kindle` が無い | https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/arm/fsl.yaml |
| EPDC コントローラ | ❌ | `epdc@20f4000` ノードはあるが `compatible` 無し。DRM の imx 配下にも fbdev にもドライバ無し | 上記 dtsi、https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/imx/Kconfig 、https://raw.githubusercontent.com/torvalds/linux/master/drivers/video/fbdev/Makefile |
| EPDC ドライバ（out-of-tree） | ⚠️ | Kemnade 氏の `mxc-epdc`。i.MX6SLL の Kobo Libra H2O で display works、i.MX6SL も RFC で対象。PW2 実績なし | https://github.com/torytyler/kobo-libra-h2o-nura-linux-display-drivers 、https://www.mail-archive.com/dri-devel@lists.freedesktop.org/msg383364.html |
| EPD 電源（VCOM / DISP_GATED） | ❌ | MAX77696 の `max77696-epd` が VCOM と表示レールを供給。mainline に該当ドライバ無し（Kobo の SY7636A/TPS65185 はあり） | https://raw.githubusercontent.com/paper42/kindle-linux-3.0.35-lab126/master/arch/arm/mach-mx6/board-mx6sl_wario.c 、https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/regulator/Makefile |
| 波形（waveform） | ❌ | eMMC の raw 領域を扱う board 統合が mainline に無い。`.wbf` を抽出して `request_firmware` で渡す実装が要る | 実機 `/usr/sbin/wfm_mount`、`/lib/udev/bin/waveform.sh`、https://github.com/torytyler/kobo-libra-h2o-nura-linux-display-drivers |
| タッチ（cyttsp4） | ⚠️ | v5.15 には存在、現行 master には無い（削除時期は未特定）。DT 非対応（i2c_device_id のみ）。FW ローダ（`cyttsp4_loader`）も別モジュール | https://raw.githubusercontent.com/torvalds/linux/v5.15/drivers/input/touchscreen/Makefile 、https://raw.githubusercontent.com/torvalds/linux/v5.4/drivers/input/touchscreen/cyttsp4_i2c.c |
| Wi-Fi（AR6003） | ✅ | `ath6kl_sdio` が `0x0300`/`0x0301` に対応。linux-firmware に `ath6k/AR6003/hw2.1.1/*` あり | https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/ath/ath6kl/sdio.c 、https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/WHENCE |
| 燃料計 / 充電 | ❌ | `max77696-battery` / `max77696-charger` 相当が mainline に無い | https://raw.githubusercontent.com/torvalds/linux/master/drivers/power/supply/Makefile |
| フロントライト | ❌ | `max77696-bl` 相当が mainline に無い | https://raw.githubusercontent.com/torvalds/linux/master/drivers/leds/Makefile |
| RTC | ❌ / 代替可 | `max77696-rtc` は無いが、SoC の SNVS RTC が mainline にあるので代替可能 | 上記 dtsi の `snvs_rtc` |
| 電源キー | ⚠️ | `max77696-onkey` は無い。GPIO 直結なら `gpio-keys` で代替できるが、配線は未確認 | 実機、推測 |
| サスペンド / 復帰 | ✅ SoC / ⚠️ ボード | `arch/arm/mach-imx/pm-imx6.c` に `imx6sl_pm_data` があり STANDBY / MEM に対応。ただし wake source（電源キー、RTC、SDIO）は PMIC 依存 | https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/arm/mach-imx/pm-imx6.c |
| ブートローダ | ✅ barebox | barebox に Kindle 6/7（PW2/PW3/Kindle 7/Voyage）ボードがあり、eMMC boot0 へ置換可能 | https://barebox.org/doc/latest/boards/imx/amazon-kindle-6-7.html |

**表示まわりの要点（(1) の可否を分けるところ）**

- PW2 の EPDC は `0x020f4000`（mainline の dtsi と同じアドレス）。EPDC 自体の IP は mainline のノード定義と一致する。【確認】
- ただし mainline の EPDC ノードは `compatible` が無く、ドライバも無い。Kemnade 氏の out-of-tree ドライバ（`drivers/gpu/drm/mxc-epdc/`）が唯一の実装。【確認】
- そのドライバは **パネル固有の波形・VCOM・SDDO バス幅・タイミング**を必要とする。Kobo Libra H2O では 16bit SDDO と GC16 フルリフレッシュの修正が要り、VCOM は SY7636A から供給している。【確認（本人報告）】
- PW2 の VCOM は **MAX77696 の `max77696-epd`**（consumer 名 `VCOM`）。mainline にこの regulator が無いので、**EPDC ドライバを移植してもパネルに電源が入らない**。【確認（Amazon ボードファイル）】
- 波形は eMMC の**パーティション外**（offset `0x1C41000` = 28.25MiB、p1 の開始 32MiB の直前までの約3.75MiB）にある。`/usr/sbin/wfm_mount` が `losetup -o 4096*7233 /dev/loop/1 /dev/mmcblk0` で FAT をマウントする。**p1〜p4 を消しても残るが、eMMC 全体の dd やパーティションテーブル変更で失う**。mainline にはこの raw 領域を読む仕組みが無い。【確認（実機のスクリプトと partition start）】

### 1.3 タッチ

- 実機の入力は `cyttsp4_mt`（I2C `1-0024`、MT protocol B、event1）。`zforce2` は宣言だけで未使用。【確認】
- mainline の cyttsp4 は v5.15 には存在し、現行 master には無い（削除時期は未特定）。**DT バインディングが無く**、`cyttsp4_i2c_id[]` のプラットフォームデータで動く旧式ドライバ。【確認】
- 実機は udev + `cyttsp4_loader` で `pinot_0108.bin` をコントローラに書き込む。diags モードでは FW 更新をスキップする実装。【確認】
- タッチの電源は MAX77696 の LSW2/LSW3（`TOUCH_VDDD` / `TOUCH_VDDA`）と L1（`ZFORCE2_TOUCH`）。**PMIC ドライバが無いとタッチも給電されない**。【確認（ボードファイル）】

### 1.4 Wi-Fi

- **AR6003**（`0271:0301`）。AR6004 ではない。mainline の `sdio_ids.h` で `0x0301` は `AR6003_01`。【確認】
- mainline `ath6kl_sdio` は AR6003 に対応し、linux-firmware に `ath6k/AR6003/hw2.1.1/`（`athwlan.bin`, `fw-2.bin`, `fw-3.bin`, `otp.bin`, `bdata.SD31/SD32/WB31.bin`, `data.patch.bin`）がある。【確認】
- 実機の Amazon ドライバは `/opt/ar6k/target/AR6003/hw2.1.1/bin/` を使い、`board_data_file` に `AR6003_wfo_calfile.bin` を指定している。**単体の校正データ（bdata/cal）は機体ごとに意味があるので、移植時は実機から退避して使う**のが安全。【確認（パスとパラメータ）/ 推測（校正の機体依存）】
- Wi-Fi の電源は GPIO `MX6_WARIO_WIFI_PWD`（"wifi_pwr"）で制御されている。mainline では fixed-regulator か `power-down-gpios` 相当に置き換える必要がある。【確認（ボードファイル）/ 推測（置き換え方法）】
- 結論: **Wi-Fi は (1) の中では最も移植しやすい**。ただし校正データと電源 GPIO の扱いを詰める必要がある。

### 1.5 電源・サスペンド

- SoC のサスペンドは mainline にある（`imx6sl_pm_data`, STANDBY/MEM、DDR LPM、`imx6sl_set_wait_clk`）。【確認】
- しかし PW2 の wake source は MAX77696 の onkey IRQ と RTC。mainline にこれらのドライバが無いので、**「電源キーで復帰」「RTC で復帰」がそのままでは動かない**。【推測（ドライバ不在からの帰結）】
- barebox のボードページは「バッテリー駆動で電源スイッチが無い。standby で iomux の pull-up を残すと電池を食う」と警告している。mainline 移植では pinmux の suspend 状態まで詰める必要がある。【確認（barebox の記述）】

---

## 2. 事例

### 2.1 (1) 完全置き換えの事例

| 事例 | 機種 / SoC | 状態 | 示唆 |
|---|---|---|---|
| kindle4nt-linux | Kindle 4 Non-Touch（i.MX50） | mainline 6.6.157 + barebox + Alpine。**K4 NT 専用**。波形はパネルフラッシュから取得、AR6003 の FW/cal は実機バックアップから | 「フル置き換え」は可能だが、**機種ごとに個別ポート**。他機種はサポート外と明記 |
| postmarketOS / Nura の Kindle 4 | Kindle 4（i.MX508） | testing、prebuilt なし、Display/WiFi/Battery Broken（2024 時点） | 唯一の Kindle pmOS ページですらこの状態 |
| Kobo Libra H2O（Nura） | i.MX6SLL | `mxc-epdc` + パネル固有 DTS で **display works**。カーネルは `linux-postmarketos-imx-ereader` 7.1.10 | 同系 SoC で「画面が出る」実例。ただし EPDC ドライバは out-of-tree、EPD PMIC は SY7636A |
| Kobo Clara 2E | i.MX6SLL | mainline DTS（`imx6sll-kobo-clara2e-b.dts`）があり、EPD PMIC は JD9930 として mainline で扱われている。EPD ドライバ本体は依然 out-of-tree | 周辺（regulator/DTS）から mainline 化が進む |
| **PW2** | i.MX6SL | **ポート無し**。barebox のみ対応 | 参照できる同型機の実例が無い |

- **i.MX6SL ちょうどの e-reader 実例は見つからなかった。** 一番近いのは i.MX6SLL の Kobo Libra H2O / Clara 2E。EPDC の IP は同系で、Kemnade 氏の RFC は両方（6SL/6SLL）を対象にしている。【確認（検索範囲）/ 推測（IP の共通性）】
- Kindle 4 の mainline DTS/バインディングを追加する 2 パッチが 2026 年に ML に投稿されている（記事時点ではマージ済みとは書かれていない）。【確認】
- MobileRead の「PW2 postmarketOS?」スレッドは 2024-05 に投稿されただけで返信が無い。【確認】

**ブラウザをどうしているか（(1) の場合）**: フル置き換え系の OS は、Kobo/Kindle とも **NetSurf**（InkBox/Quill）か、Alpine 上の **Chromium**（512MB 以上）が主流。GPU は無いのでソフトウェアレンダリング。PW2 の 256MB では Chromium は現実的でなく、NetSurf/links2 級になる。【確認（InkBox/Quill、alpine_kindle）/ 推測（PW2 の RAM）】

### 2.2 (2) 半分置き換えの事例

| 事例 | 内容 | PW2 への示唆 |
|---|---|---|
| **KOReader no-framework（本機）** | `koreader.sh --framework_stop` → `stop lab126_gui`。v2026.07.1 が稼働 | **本機で実証済み**。自作 UI も同じ土台に乗る |
| **khome（本リポジトリ）** | C で `/dev/fb0` に描画。現在 pid 26329 で稼働 | 自作ホームの実装が既にある |
| alpine_kindle（schuhumi） | PW3 で Alpine chroot + XFCE + Chromium。**512MiB RAM 以上**、2GB イメージ、upstart で Kindle GUI を止めて RAM を節約 | PW2 は RAM 不足。GUI なしなら可 |
| kindle-linux-chroot（Sqrilizz） | Debian/Alpine/Arch の ext3 イメージ + KUAL + kterm。**ヘッドレス**。Kindle 10th gen でテスト、PW3+ で動く想定 | PW2 でもヘッドレス chroot は可（RAM と相談） |
| XDA の Debian chroot 記事（2026-10） | 1GB イメージ、apt、SSH。PW12/FW 5.19.6 | 「chroot は簡単・安全」の実例 |
| InkBox / Quill OS | Alpine 3.10 + Qt のネイティブ OS。Kobo と「some Kindle」。**NetSurf ブラウザ**、X11 サブシステム | Kindle の対応機種が不明。PW2 対応は未確認 |

- なお **「kindle-alt OS」という名称の公開プロジェクトは見つからなかった**。文脈上近いのは InkBox/Quill OS（Alpine + Qt のネイティブ OS。Kobo 中心、Kindle は一部）と kindle4nt-linux（mainline + barebox。K4 NT 専用）。【確認（検索範囲）】
- **(2) の要点**: カーネルを触らないので、EPDC / 波形 / タッチ / Wi-Fi / フロントライト / 充電が**そのまま動く**。純正 UI（Java/cvm, X, awesome, mesquite）を止めても `powerd` / `wifid` / `lipc` は残せる。【確認（upstart の依存関係）】
- **起動時から自作 UI** にする場合の注意: `lab126_gui` を止めると `kb` / `pillow` / `kfxreader` / `kfxview` / `contentpackd` も止まる。`framework_ready` に依存する `dmld` / `bridge` / `chroot` / `acxmgrd` も止まる。どれを残すかは用途次第（今回の目標には不要）。【確認】

### 2.3 純正 UI 無しでブラウザを動かす手段

| 手段 | 現実性 | 根拠 |
|---|---|---|
| **NetSurf framebuffer (`netsurf-fb`)** | ◎ 本命。X 不要、8bpp のまま動く。`-w 600 -h 800 -b 8` の実例あり（PW3） | MobileRead 323890 |
| links2 / w3m / lynx（テキスト） | ○ 軽い。CSS/JS は無い | 一般知識 |
| Alpine chroot + Xorg(fbdev) + Chromium | △ PW3 512MB で実績。PW2 256MB では RAM 不足。X の 8bpp 問題もあり | alpine_kindle、XDA |
| InkBox の NetSurf | ○ Kobo で実績。Kindle 対応は機種限定 | Quill README |
| 純正 WAF ブラウザ（mesquite/WebKit） | × 今回の「純正以外」条件に反する。FW 5.12 の WebKit は古い | 既存調査 `docs/spikes/device-survey.md` |

- **結論**: 純正 UI を起動しない構成でも、**NetSurf framebuffer が現実的な唯一の「まともな」ブラウザ**。Chromium 級を求めるなら PW3 以降（512MB）が必要。【推測を含む】

---

## 3. 戻し方とリスク

### 3.1 リカバリ経路（PW2）

| 経路 | 前提 | 手順の要点 | 出典 |
|---|---|---|---|
| **diags パーティション** | 内蔵 eMMC の p2（65MB）が diags（**推測**：サイズと一般的なレイアウト、gist の `pw_5.2.0-mmcblk0p2.img` から） | u-boot で `bist fastboot` → `fastboot setvar bootmode diags` → diags の USB モードで `update.bin` を置いて再起動 | gist、MobileRead |
| **fastboot** | u-boot に入れること（serial か既存の fastboot 経路） | `bist fastboot` → `Fastboot-Kindle` で `flash system/kernel/diags`、`setvar bootmode diags` | gist、Fastboot-Kindle |
| **シリアルコンソール（USB コネクタ方式）** | **分解不要**。Micro-USB の sense ピンに 30kΩ を GND へ。D- = TXD, D+ = RXD | 115200bps で u-boot を中断（`console=ttymxc0,115200` は実機 cmdline で確認） | barebox の Kindle 6/7 ページ |
| **シリアルコンソール（はんだ方式）** | 分解 + シリアルパッドにはんだ | GND/RX/TX、115200bps、u-boot で Enter 連打 | MobileRead 335953、gist |
| **USB SDP（imx_usb_loader）** | **分解 + 金属シールドの取り外し + eMMC を一時無効化**（DP75SDI は TP806/TP807 の左の抵抗を 0Ω に）。ボタンパッドは未実装、pin-strap はヒューズで無効 | 電源長押しで OFF → 離す → `SE Blank MEGREZ` が出たら `imx-usb-loader` で barebox を RAM ロード | barebox の Kindle 6/7 ページ |
| **barebox を入れる場合** | imximg が 384,000 bytes に収まること。eMMC のシリアル番号等を壊さない | `memcpy -s barebox.img -d /dev/mmc1.boot0 0x400 0x400 0x5dc00` | barebox の Kindle 6/7 ページ |

- 実機には `fw_printenv` が無く、`/proc/mtd` も無い。u-boot 環境変数は Linux から読めない。**作業前に u-boot に入る手段（USB シリアル or fastboot）を必ず確保する**。【確認】
- `idme` は `/usr/sbin/idme` にある（bootmode の設定に使われる）。`/proc/bootmode` は現在 `main`。【確認】

### 3.2 文鎮化・失敗の事例

- PW2 が既に bricked で、分解してシリアルパッドにはんだして復旧を試みた事例（u-boot までは入れるが diags に入れない）。【確認】
- PW3（6th gen）の bootloop を Amazon の update ファイル方式で復旧した例、VM 上の fastboot では失敗してネイティブ Debian で成功した例。【確認】
- PW5（11th gen）はシリアルパッドが無く、復旧が難しいという報告。【確認】
- `alpine_kindle` の README は、**Alpine 稼働中に USB マスストレージとして PC と同時に p4 へ書き込むとパーティションが壊れ、文鎮化し得る**と警告している。【確認】

### 3.3 バックアップすべきもの（(1)(2) 共通）

1. **eMMC 全体**（boot0, boot1, パーティションテーブル, p1〜p4, 波形の raw 領域）。boot0 を上書きすると純正の復旧経路を失う（barebox の記述、kindle4nt の警告）。【確認】
2. **`/var/local/eink/waveforms/`**（`320_G135_AA5431_ED060XD4U1_TC.wbf` 456,570 bytes と `.wrf.gz` 89,007 bytes）。パネル固有。**消すと表示が出なくなる**。実体は eMMC の raw 領域（offset `0x1C41000`、約3.75MiB）なので、`/var/local` のコピーだけでは不十分。raw 領域も退避する。【確認（実機）】
3. **`/opt/ar6k/target/AR6003/hw2.1.1/bin/`**（Wi-Fi FW と校正データ）。【確認（実機）】
4. **`/lib/firmware/cyttsp4/pinot_0108.bin`**（タッチ FW）。【確認（実機）】
5. `/etc/upstart/` の変更前コピー（(2) でジョブを追加する場合）。
6. `/mnt/us` の自作物（khome, KOReader, extensions）。

### 3.4 リスク一覧

| リスク | 影響 | 対策 |
|---|---|---|
| MAX77696 ドライバが無い | (1) でパネル電源・VCOM・タッチ電源・フロントライト・充電が全部死ぬ | (1) を選ばない。選ぶなら最初に MFD/regulator を書く |
| EPDC が out-of-tree | カーネル更新のたびに rebase が必要 | Kemnade 氏のブランチに追従 |
| 波形の消失 | 画面が更新されない/真っ黒 | `/var/local/eink/waveforms` と eMMC の raw 領域（offset `0x1C41000`）を退避 |
| p4（/mnt/us）破損 | 起動不能・文鎮化 | Alpine/chroot 稼働中は USBMS にしない |
| upstart ジョブの失敗 | 起動ループ、customer service 画面 | `lab126_gui` には再起動回数のガード（`MAX_REBOOTS=2`）があり、失敗が続くと機内モード化や customer service 画面への移行がある。ジョブに timeout と fallback を入れ、KUAL/diags を残す |
| OTA による上書き | `/etc/upstart` の変更が消える | 既に `renameotabin` が入っている。無効化を維持 |
| カーネル 3.0.35 のまま | セキュリティ更新なし、新しい API なし | (2) のトレードオフとして受容。ユーザーランド側で TLS を新しくする |
| サスペンド中の電池消費 | 数日で空 | `preventScreenSaver` / `deferSuspend` を自作 UI から制御（既存調査の LIPC 知見） |

---

## 4. 端末で確認した事実

すべて `ssh -o BatchMode=yes kindle` で実行。`PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin`。**書き込み・再起動・サービス操作はしていない。**

### 4.1 実行コマンドと結果（抜粋）

```text
$ uname -a
Linux kindle 3.0.35-lab126 #2 PREEMPT Tue Mar 2 17:21:29 UTC 2021 armv7l GNU/Linux

$ cat /proc/cpuinfo
Processor : ARMv7 Processor rev 10 (v7l)
Features  : swp half thumb fastmult vfp edsp neon vfpv3
CPU part  : 0xc09
Hardware  : Freescale i.MX 6SoloLite based Wario Board
Revision  : 60000
```

```text
$ lsmod
ath6kl_sdio 198006 0
cfg80211 171711 1 ath6kl_sdio
cyttsp4_mt_b 8204 0
cyttsp4_core 41388 2
cyttsp4_i2c 2587 1
mxc_epdc_fb 89869 4
mxc_epdc_eink 8771 1 mxc_epdc_fb
...
$ ls /lib/modules/*/
extra  kernel  modules.alias ... （3.0.35-lab126 のみ）
```

```text
$ cat /proc/bus/input/devices
N: Name="max77696-onkey"   H: Handlers=kbd event0   B: EV=3   B: KEY=100000 0 0 0
N: Name="cyttsp4_mt"       H: Handlers=event1      B: EV=f   B: ABS=2608000 0
```

```text
$ ls /sys/class/backlight/
max77696-bl -> .../max77696-bl.0/backlight/max77696-bl
$ ls /sys/class/power_supply/
max77696-battery  max77696-charger  max77696-eh  max77696-uic
$ cat /sys/class/backlight/max77696-bl/max_brightness
4095
```

```text
$ dmesg | grep -iE "epdc|wifi|wlan|sdio|ath|bcm|mwifiex|cyttsp|max77696" | head -50
[ 5633.086742] wifilocker (16435): undefined instruction: pc=40386228
... （wifilocker 13 行のみ。ハード関連 0 行）
```

```text
$ cat /proc/mtd
cat: can't open '/proc/mtd': No such file or directory
$ cat /proc/partitions
179 0 3866624 mmcblk0
179 1 358400 mmcblk0p1
179 2 65536 mmcblk0p2
179 3 65536 mmcblk0p3
179 4 3344384 mmcblk0p4
179 8 2048 mmcblk0boot0
179 16 2048 mmcblk0boot1
$ fw_printenv
sh: fw_printenv: not found
```

```text
$ cat /proc/cmdline
rootwait log_pos_goal=0x8fff0000 console=ttymxc0,115200 root=/dev/mmcblk0p1 ro ip=off quiet video=mxcepdcfb:E60,bpp=8,x_mem=3M
```

```text
$ cat /sys/bus/sdio/devices/mmc1:0001:1/uevent
DRIVER=ath6kl_sdio
SDIO_ID=0271:0301
MODALIAS=sdio:c00v0271d0301
```

```text
$ cat /sys/class/power_supply/max77696-battery/uevent
POWER_SUPPLY_CYCLE_COUNT=62
POWER_SUPPLY_VOLTAGE_NOW=4059
POWER_SUPPLY_CHARGE_FULL_DESIGN=790000
POWER_SUPPLY_CHARGE_FULL=1408
POWER_SUPPLY_CHARGE_NOW=1233
POWER_SUPPLY_CAPACITY=85
POWER_SUPPLY_TEMP=78
POWER_SUPPLY_CURRENT_NOW=-128
```

```text
$ ls /var/local/eink/waveforms/
320_G135_AA5431_ED060XD4U1_TC.wbf      456570
320_G135_AA5431_ED060XD4U1_TC.wrf.gz    89007
$ ls /lib/firmware/cyttsp4/
icewine_0017.bin  muscat_000E.bin  pinot_0108.bin  whisky_0071.bin
$ find /opt/ar6k -maxdepth 5
/opt/ar6k/target/AR6003/hw2.0/bin/...
/opt/ar6k/target/AR6003/hw2.1.1/bin/{athwlan.bin,bdata.bin,otp.bin,...}
```

```text
$ cat /usr/sbin/wfm_mount
...
losetup -o $(( 4096 * 7233 )) /dev/loop/1 /dev/mmcblk0
mount /dev/loop/1 /mnt/wfm
$ for p in 1 2 3 4; do cat /sys/block/mmcblk0/mmcblk0p$p/start; done
65536   # p1 = 32MiB
782336
913408
1044480
# 波形は sector 57864 = 28.25MiB から p1 直前まで（約3.75MiB、パーティション外）
```

```text
$ /sbin/modinfo ath6kl_sdio
firmware: ath6k/AR6004/hw1.1/fw.ram.bin ...
firmware: /opt/ar6k/target/AR6003/hw2.1.1/bin/athwlan.bin ...
$ cat /sys/module/ath6kl_sdio/parameters/board_data_file
/opt/ar6k/target/AR6003/hw2.1.1/bin/AR6003_wfo_calfile.bin
```

```text
$ cat /proc/bootmode
main
$ df -h /        -> 348.1M / 309.5M used / 31.6M avail (91%)
$ df -h /mnt/us  -> 3.2G / 3.1G avail
$ free           -> Mem: 254828 total (249MB), no swap
$ ps -eo pid,rss,args | sort -k2 -nr | head
4772 78024 /usr/java/bin/cvm ...      # 純正ホーム (Java)
26329 23172 /mnt/us/khome/bin/khome   # 自作ホーム
2615   7496 Xorg ...
```

### 4.2 端末から言えること

- **PW2（Pinot/Wario）で間違いない**（`/etc/version.txt`, `/proc/cpuinfo`, barebox の型番）。ブリーフの前提と一致する。
- SoC / PMIC / タッチ / Wi-Fi / 燃料計 / フロントライト / パネルの型番は sysfs と FW から**特定できた**。
- **起動ログは残っていない**（dmesg は起動後の SIGILL で埋まっている）。ハードの詳細を dmesg から取ることはできない。
- **`/` は 91% 使用で残り 31.6MB**。ユーザーランドを増やすなら `/mnt/us`（3.1GB 空き）に置くしかない。
- **khome と KOReader が既に動いている**ので、(2) の「純正 UI を止めて自作 UI」は机上の話ではない。

---

## 5. 未確認の点

1. **PW2 の diags が p2 か**。サイズ（65MB）と gist の `mmcblk0p2` から推測したが、マウントして中身を確認していない（読み取りのみの制約）。
2. **USB コネクタの 30kΩ シリアルが PW2 でも使えるか**。barebox の記述は gen6/7 全体に対するもの。実機で試していない。MobileRead の PW2 事例は「分解してパッドにはんだ」している。
3. **MAX77696 の完全なデータシート/レジスタ仕様**。公開資料は Product Brief レベル。Amazon の GPL ソース（`max77696-core.c` ほか）が実質的な仕様書になる。
4. **`mxc-epdc` が i.MX6SL + ED060XD4U1 で動くか**。Kemnade 氏の RFC は i.MX6SL を対象に含むが、PW2 での公開実績は無い。パネルタイミング・SDDO 幅・VCOM の詰めが必要。
5. **波形 raw 領域の正確な範囲と `panel_flash_spi` の役割**。`wfm_mount` は sector 57,864 から p1 直前までを使うと読めるが、FAT の終端・サイズは未確認。`spi0.0 panel_flash_spi` が何のためにあるかも未特定（波形は eMMC 側にある）。
6. **AR6003 の実行時 FW の出所**。`/lib/firmware/ath6k/` が無いのに wlan0 が動いている。カーネル内蔵 FW（CONFIG_EXTRA_FIRMWARE）か、別の隠しパスの可能性。移植時に要特定。
7. **`/mnt/us/DONT_START_FRAMEWORK` だけ置いた場合の画面**。framework は止まるが X/awesome/blanket は残る。自作 UI が上書きできるか、blanket が再描画しないかは未検証。
8. **`lab126_gui` を完全に止めた状態の安定性**。`lab126_gui_monitor` や `x_cleanup` の挙動、OTA 時の影響を実機で試していない（指示により stop していない）。
9. **NetSurf framebuffer が PW2 で動くか**。PW3 の報告のみ。8bpp・758×1024・タッチ入力の組み合わせは未検証。
10. **Xorg + fbdev の 8bpp で Chromium が動くか**。一般に Chromium は 24/32bpp 前提。仮に動いても 256MB では足りない見込み。
11. **モジュール署名の有無**。Amazon カーネルに自前ビルドのモジュールをロードできるか（`CONFIG_MODULE_SIG_FORCE`）は未確認。3.0.35 の Amazon GPL ソースからビルドできる可能性はある。
12. **zforce2 が何のために宣言されているか**。PW2 の実タッチは cyttsp4 だが、`wario_1.0b0r8.hex` も存在する。別 SKU 用の残骸と推測。
13. **pmOS の「Nura」への改名の範囲**。`nura.eco` / `pkgs.nura.eco` / `wiki.nura.eco` は確認したが、プロジェクト全体の正式名称変更か、配布ブランドかは未確認。

---

## 6. 出典一覧

### 一次情報（カーネル・ブートローダ・ファームウェア）

- mainline `imx6sl.dtsi`（EPDC ノードに compatible が無い）: https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/arm/boot/dts/nxp/imx/imx6sl.dtsi
- mainline `drivers/gpu/drm/imx/Kconfig`（EPDC が無い）: https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/imx/Kconfig
- mainline `drivers/video/fbdev/Makefile`: https://raw.githubusercontent.com/torvalds/linux/master/drivers/video/fbdev/Makefile
- mainline `drivers/mfd/Makefile`（max77696 が無い）: https://raw.githubusercontent.com/torvalds/linux/master/drivers/mfd/Makefile
- mainline `drivers/regulator/Makefile`（SY7636A / TPS65185 はある、max77696 は無い）: https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/regulator/Makefile
- mainline `drivers/power/supply/Makefile`: https://raw.githubusercontent.com/torvalds/linux/master/drivers/power/supply/Makefile
- mainline `drivers/rtc/Makefile`: https://raw.githubusercontent.com/torvalds/linux/master/drivers/rtc/Makefile
- mainline `drivers/leds/Makefile`: https://raw.githubusercontent.com/torvalds/linux/master/drivers/leds/Makefile
- mainline `drivers/input/misc/Makefile`: https://raw.githubusercontent.com/torvalds/linux/master/drivers/input/misc/Makefile
- mainline `pm-imx6.c`（i.MX6SL suspend）: https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/arm/mach-imx/pm-imx6.c
- mainline `ath6kl/sdio.c`（AR6003/AR6004 の ID）: https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/ath/ath6kl/sdio.c
- mainline `sdio_ids.h`（0x0301 = AR6003_01）: https://raw.githubusercontent.com/torvalds/linux/v6.12/include/linux/mmc/sdio_ids.h
- mainline `fsl.yaml`（wario/pinot/kindle が無い）: https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/arm/fsl.yaml
- cyttsp4 が v5.15 に存在: https://raw.githubusercontent.com/torvalds/linux/v5.15/drivers/input/touchscreen/Makefile
- cyttsp4_i2c に DT が無い: https://raw.githubusercontent.com/torvalds/linux/v5.4/drivers/input/touchscreen/cyttsp4_i2c.c
- Kobo Clara 2E DTS（JD9930 EPD PMIC、EPDC ノードは無い）: https://raw.githubusercontent.com/torvalds/linux/master/arch/arm/boot/dts/nxp/imx/imx6sll-kobo-clara2e-b.dts
- Kobo Libra H2O DTS（SY7636A、EPDC ノードは無い）: https://raw.githubusercontent.com/torvalds/linux/master/arch/arm/boot/dts/nxp/imx/imx6sll-kobo-librah2o.dts
- linux-firmware WHENCE（AR6003 hw2.1.1 / AR6004）: https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/WHENCE
- barebox「Amazon Kindle 6/7 (Paperwhite 2/3, Kindle 7th gen and Voyage)」: https://barebox.org/doc/latest/boards/imx/amazon-kindle-6-7.html
- Amazon GPL カーネル（Kindle 3.0.35）: https://github.com/paper42/kindle-linux-3.0.35-lab126
  - ボードファイル: https://raw.githubusercontent.com/paper42/kindle-linux-3.0.35-lab126/master/arch/arm/mach-mx6/board-mx6sl_wario.c
  - MAX77696 MFD: https://raw.githubusercontent.com/paper42/kindle-linux-3.0.35-lab126/master/drivers/mfd/max77696-core.c
  - EPDC fb: https://raw.githubusercontent.com/paper42/kindle-linux-3.0.35-lab126/master/drivers/video/mxc/mxc_epdc_fb.c

### EPDC / 電子ペーパー

- Kemnade「[RFC PATCH 0/6] drm: EPDC driver for i.MX6」: https://www.mail-archive.com/dri-devel@lists.freedesktop.org/msg383364.html
- Kemnade「[PATCH 0/3] ARM: dts: imx: wire up the TPS65185 ...」（2026-01、「EPD 本体のドライバはまだ polishing が必要」）: https://lkml.org/lkml/2026/1/13/1575
- Kobo Libra H2O / Nura の EPDC 表示（mxc-epdc、display works）: https://github.com/torytyler/kobo-libra-h2o-nura-linux-display-drivers
- Kobo Clara 2E DTS: https://lkml.org/lkml/2024/10/24/1067
- Nura の `linux-postmarketos-imx-ereader` 7.1.10（Kemnade 氏メンテ）: https://pkgs.nura.eco/package/main/postmarketos/armv7/linux-postmarketos-imx-ereader

### Kindle の完全置き換え事例

- kindle4nt-linux（K4 NT、mainline 6.6 + barebox + Alpine）: https://github.com/qwert-Xx/kindle4nt-linux
- postmarketOS Kindle 4 ページ（2024-05-25 の Wayback）: https://web.archive.org/web/20240525030012/https://wiki.postmarketos.org/wiki/Amazon_Kindle_4_(amazon-yoshi)
- postmarketOS の Kindle 4 デバイス一覧（Category:Devices）: https://wiki.postmarketos.org/wiki/Category:Devices
- Phoronix「Kindle 4 mainline パッチ」: https://www.phoronix.com/news/Amazon-Kindle-4-Linux-Patches
- MobileRead「PW2 postmarketOS on Kindle ?」（返信なし）: https://www.mobileread.com/forums/showthread.php?t=361212

### 半分置き換え・chroot・ブラウザ

- KOReader（Kindle の no-framework モード）: https://github.com/koreader/koreader
- alpine_kindle（PW3、512MiB RAM、XFCE+Chromium、upstart で GUI 停止）: https://github.com/schuhumi/alpine_kindle
- kindle-linux-chroot（Debian/Alpine/Arch、ヘッドレス）: https://github.com/Sqrilizz/kindle-linux-chroot
- XDA「I installed Debian on my Kindle」（2026-10）: https://www.xda-developers.com/i-installed-debian-on-kindle-and-its-pocketable-terminal-for-home-server/
- XDA「My unsupported Kindle runs Linux now」（PW3、Alpine chroot + Chromium）: https://www.xda-developers.com/my-unsupported-kindle-runs-linux-now-and-its-more-useful-than-when-it-was-new/
- NetSurf framebuffer のビルド: https://github.com/netsurf-browser/netsurf/blob/master/docs/building-Framebuffer.md
- MobileRead「netsurf-fb -w 600 -h 800 -b 8 で動いた」: https://www.mobileread.com/forums/showthread.php?t=323890
- InkBox / Quill OS（Alpine + Qt、NetSurf）: https://github.com/Quill-OS/quill

### 復旧

- gist「How to Unbrick a Kindle Paperwhite」（serial → `bist fastboot` → Fastboot-Kindle → diags → update.bin）: https://gist.github.com/ramast/58633c352ad78e240c2ecc96965d48ed
- MobileRead「PW2 Unbricking w/ USB and Serial」（PW2 のシリアルパッド）: https://www.mobileread.com/forums/showthread.php?t=335953
- Fastboot-Kindle: https://github.com/TobiasWooldridge/Fastboot-Kindle
- imx_usb_loader（SDP）: https://imxdev.gitlab.io/tutorial/Using_the_imx_usb_loader_tool/
- KindleModding（KT2 の SDP テストパッド例）: https://kindlemodding.org/jailbreaking/Legacy/Popcorn/KT2.html
- KindleModding Wiki（総合）: https://kindlemodding.org/

### 部品

- MAX77696（E-Reader PMIC、Product Brief）: https://www.mobileread.com/forums/attachment.php?attachmentid=130799&d=1415512444
- MAX77696（DigiKey、Highly Integrated PMIC for Ereader Applications）: https://www.digikey.com/en/products/base-product/analog-devices-inc-maxim-integrated/175/MAX77696/324287
