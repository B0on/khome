# フレームワーク停止スパイク（lab126_gui を止めた状態の実機確認）

2026-10-23。Paperwhite 2（758×1024）/ FW 5.12.2.1.1 / `ssh kindle`（<kindle-ip>, root）。
コマンドはすべて先頭に `export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin DISPLAY=:0;` を付けて実行した。
時刻は端末の `/var/log/messages` に合わせて UTC。画面は `scripts/dev/kshot.sh` で採取（画像は `scripts/dev/shots/`、git 管理外）。

## 結論（要約）

- `stop lab126_gui` で止まるのは **cvm（framework）と lab126_gui 配下のジョブだけ**。Xorg / awesome / blanket / powerd / appmgrd / wifid は残る（`x` と `lab126` は別ジョブ）。
- GUI 停止中も **powerd・Wi-Fi・フロントライト・電池・タッチ入力・eips 描画は動く**。
- ただし **電源ボタンは powerd が `Splash screen is on. Ignoring power button` で無視する**。GUI 稼働中なら同じ `powerButton 1` で 2 秒以内に `screenSaver` に入り、5.7 秒で復帰できた。停止中は 2 回試して 2 回とも無視され、`goingToScreenSaver` は一度も来なかった。
- GUI 停止中も **idle タイマーは減り続ける**（10 秒で 10.05 秒減）。自動スリープの危険は残る。
- `DONT_START_FRAMEWORK` は cvm を確実に止めるが、**スプラッシュが降りず画面は使えない**。ブラウザのプロセス（mesquite）は cvm 無しでも起動するが、chromebar が無く、画面はスプラッシュのまま。
- `start lab126_gui` から純正ホームが出るまで **約 82〜100 秒**。`framework_setup` の `FrameworkStarted` 待ちは 105 秒なので余裕が小さい。
- 再起動監視については、**手動の `stop` / `start` では再起動カウントは増えない**（下記 0 節）。今回は stop/start を 3 往復して reboot は起きず、カウンタも空のまま。

## 0. 再起動監視（MAX_RESTARTS / MAX_REBOOTS）の解析

読んだファイル: `/etc/upstart/lab126_gui.conf`、`/etc/upstart/lab126_gui_monitor.conf`、`/etc/upstart/monitor_functions`。

`lab126_gui.conf`（102 行、`pre-start script` のみのタスク）:

```text
stop on stopping x or ota-update
env MAX_RESTARTS=3
env MAX_REBOOTS=2
...
REBOOTS=$(f_monitor_persist_read_count ${UPSTART_JOB})
if [ "$REBOOTS" -ge "$MAX_REBOOTS" ]; then ... customer service page ...
...
if [ -z $MONITOR_RESTART ]; then
    # if we're not being restarted by the monitor, then clear the session count and exit
    f_monitor_session_clear_count ${UPSTART_JOB}
    exit 0
fi
RESTARTS=$(f_monitor_session_read_count ${UPSTART_JOB})
if [ "$RESTARTS" -ge "$MAX_RESTARTS" ]; then
    REBOOTS=$(f_monitor_persist_inc_count ${UPSTART_JOB})
    ... stop; reboot
```

`lab126_gui_monitor.conf`:

```text
start on (stopped kb RESULT!=ok) or (stopped framework RESULT!=ok) or (stopped framework_setup RESULT!=ok)
stop on stopping x
...
f_monitor_session_inc_count ${RESTART_JOB}
restart $RESTART_JOB MONITOR_RESTART=1 || ...
```

カウンタの実体（`monitor_functions`）: session = `/var/run/upstart/<job>.restarts`、persist = `/var/local/upstart/<job>.restarts`。

**結論:**

- 再起動（reboot）に進むのは **`MONITOR_RESTART=1` で monitor から restart されたとき**だけで、かつ session カウントが 3 以上になったとき。
- 手動の `start lab126_gui` は `MONITOR_RESTART` が空なので、pre-start の途中で **session カウントを消して exit** する。したがって手動 stop/start を何往復しても RESTARTS は増えない。
- monitor が動くのは `kb` / `framework` / `framework_setup` が **RESULT!=ok（異常終了）** で止まったときだけ。手動 `stop lab126_gui` による子の停止は RESULT=ok なので monitor は動かない。
- 実行前のカウンタ: `/var/local/upstart/` は空（persist=0）、`/var/run/upstart/` も空。`MAX_REBOOTS=2` に対して 0。
- 実行後のカウンタ: `/var/local/upstart/` は空のまま。`/var/run/upstart/` に `stored.restarts` だけができた（`stored` 自身の監視。`lab126_gui.restarts` は無い）。`/var/log/messages` に `abnormalstop` / `rebooting system` / `restarts=` は出ていない。

判定: **OK（手動 stop/start では reboot しない）**。ただし異常終了を繰り返すと 3 回で reboot する仕組みは生きている（リスク節）。

## 1. khome を SIGTERM で終了

```sh
kill -TERM $(pidof khome)
```

- `khome` は 4 秒で消えた。`/mnt/us/khome/khome.log`:

```text
14:53:35 khome: quit requested
14:53:35 release: restoring framework
14:53:39 fb dump: restored
14:53:39 khome: exit 0
```

- 終了後: `cvm` pid 4772 = **S**（T ではない）、`awesome` pid 3045 = **S**。`activeApp=com.lab126.booklet.home`。
- 画面: `scripts/dev/shots/01-after-khome-term.png` に純正ホーム（My Kindle / マイライブラリ）。直前の khome 画面は `00-baseline.png`。

判定: **OK**。

## 2. `stop lab126_gui`

```sh
/sbin/stop lab126_gui   # rc=0
```

- 所要時間: `/proc/uptime` 10702.48 → 10702.87 = **0.39 秒**。
- `ps` の行数 94 → 87。`free`:

```text
         total   used    free   shared buffers cached
before  254828  225780   29048     0     6900   58284
after   254828  124532  130296     0     6940   54944
```

（cvm の終了で約 100MB 戻る）

- `initctl list` の差分で stop/waiting になったジョブ（13 件）:
  `dmld`, `pillow`, `poll_daemons`, `webreader`, `framework`, `stored`, `kb`, `kfxreader`, `contentpackd`, `bridge`, `scanner`, `kfxview`, `lab126_gui`
- 残った主なプロセス: `Xorg`(2615, S), `awesome`(3045, S), `blanket`(3046, S), `powerd`(1894, S), `appmgrd`(1899, S), `wifid`(1954, S), `wpa_supplicant`(4970), `udhcpc`(8515), `dropbear`。**cvm は消えた**。
- `lab126_gui_monitor` は stop/waiting のまま（monitor は発火していない）。
- `appmgrd activeApp` は **`com.lab126.booklet.home` / pid 4772 のまま（死んだ pid の残骸）**。停止中はこのプロパティを信用できない。
- 画面: `scripts/dev/shots/02-gui-stopped.png` — **全面白**。誰も描かないので直前のホームは残らず、framebuffer は白くなった（`eips -c` 前の時点で既に白）。

判定: **OK（0.4 秒で止まる）**。

## 3. GUI 停止中に確かめた機能

### 3.1 powerd / 画面消灯（スクリーンセーバー）

`lipc-wait-event -m -t com.lab126.powerd '*'` を裏で流し、「`lipc-set-prop -i com.lab126.powerd powerButton 1` → 3〜5 秒後に再度 `powerButton 1` で復帰を狙う」を 2 回繰り返した。

1 回目:

```text
state=active
lipc-set-prop -i com.lab126.powerd powerButton 1  -> rc=0, 0.05s
state_after=active
```

`/var/log/messages`:

```text
261023:055438 powerd[1894]: I def:pbprop:powerButton=1s:set powerButton
261023:055438 powerd[1894]: E lipc:dbuserr:name=org.freedesktop.DBus.Error.ServiceUnknown:... The name com.lab126.kaf was not provided by any .service files
261023:055438 powerd[1894]: I def:pbisplash::Splash screen is on.  Ignoring power button
```

2 回目（055459 / 055502）も同じ。`lipc-wait-event` のログは **空**（`goingToScreenSaver` / `outOfScreenSaver` は一度も来ない）。state は 5 秒後も `active`。画面は `03-screensaver-1.png` / `04-screensaver-2.png` とも白のまま（変化なし）。

参考（GUI を戻した後の追加確認、06:03）: 同じコマンドで `active → screenSaver` になり、5.7 秒後に `outOfScreenSaver` で `active` に戻った。

```text
[06:03:41.569703] goingToScreenSaver 2
[06:03:47.259351] t1TimerReset
[06:03:47.290710] outOfScreenSaver 1
[06:03:47.366581] exitingScreenSaver
```

判定: **NG（GUI 停止中は電源ボタンが無視され、スクリーンセーバーに入れない）**。理由は `pbisplash`（framework が起動していない＝スプラッシュ中とみなす）。`com.lab126.kaf` が無いこともログに出る。

### 3.2 物理の電源ボタンの入力経路

- `/proc/bus/input/devices`: `Name="max77696-onkey"` → `Handlers=kbd event0`。タッチは `cyttsp4_mt` → `event1`。
- `/proc/*/fd` を全走査して `event0` を開いているプロセス: **GUI 停止中も、GUI 稼働中も 0 件**。
- `ls -l /proc/1894/fd`（powerd）: `/dev/null`, `/dev/kmsg`, `/var/run/powerd.pid`, `/dev/frontlight`, socket/pipe のみ。**`/dev/input/*` を持っていない**。
- `grep -a` で `/proc/1894/exe` を調べても `event0` / `max77696` / `onkey` / `/dev/input/event` の文字列は 0 件。
- `event1` を開いているのは `Xorg`(2615) だけ。

**判定: 未確認（物理ボタンは押せない）**。ただし根拠から言えるのは、powerd は event0 を直接読んでおらず、電源キーの経路は Java フレームワーク側（`com.lab126.kaf`）を経由している可能性が高い、ということ。GUI 停止中は KAF が存在せず、`pbisplash` が「スプラッシュ中」と判定するので、**物理ボタンであっても同じ経路で無視される公算が大きい**。実測できたのは「ソフトウェアの powerButton は無視される」ところまで。

### 3.3 Wi-Fi

```text
$ wpa_cli -i wlan0 status | grep -E "^(ssid|wpa_state)="
ssid=<SSID>
wpa_state=COMPLETED

$ lipc-get-prop com.lab126.wifid cmState
CONNECTED

$ ping -c 1 -W 3 <gateway-ip>
1 packets transmitted, 1 packets received, 0% packet loss  (7.371 ms)

$ ping -c 1 -W 3 1.1.1.1
1 packets transmitted, 0 packets received, 100% packet loss
$ ping -c 1 -W 3 8.8.8.8
1 packets transmitted, 0 packets received, 100% packet loss
```

LAN は通る。インターネット宛の ICMP はこの環境では返らない（Wi-Fi 自体は接続済み）。SSH も切れなかった。

判定: **OK（Wi-Fi は停止中も維持）**。外部 ping の失敗は ICMP 遮断と解釈（HTTP は未試験）。

### 3.4 フロントライト

```text
flIntensity=5  flRawIntensity=14
/sys/class/backlight/max77696-bl/brightness = 14
lipc-set-prop -i com.lab126.powerd flIntensity 10  -> rc=0
  -> flIntensity=10, brightness=167
lipc-set-prop -i com.lab126.powerd flIntensity 5
  -> flIntensity=5, brightness=14（元に戻った）
```

判定: **OK**。

### 3.5 電池

```text
lipc-get-prop com.lab126.powerd battLevel   -> 84
lipc-get-prop com.lab126.powerd isCharging  -> 0
```

（スパイク終了時は 81〜82%。cvm の再起動中に消費が進んだ）

判定: **OK**。

### 3.6 タッチ（/dev/input/event1）

`timeout -t 6 dd if=/dev/input/event1 of=/tmp/survey/touch.bin bs=16` を流し、その間に `scripts/dev/ktap.sh 400 700` で 1 回タップ。

- 96 バイト（6 イベント）取得。中身:

```text
type=3 code=0x39 value=77      # ABS_MT_TRACKING_ID
type=1 code=0x14a value=1      # BTN_TOUCH down
type=0 code=0 value=0          # SYN
type=3 code=0x39 value=-1      # tracking id -1
type=1 code=0x14a value=0      # BTN_TOUCH up
type=0 code=0 value=0          # SYN
```

座標イベント（0x35/0x36）は出ていないが、直前の注入と同じ値だったため input core が重複を落としたとみられる（推測）。押下・離上は届いている。

判定: **OK（タップは event1 に届く）**。

### 3.7 画面描画（eips）

```sh
/usr/sbin/eips -c
/usr/sbin/eips 20 10 "NO FRAMEWORK TEST"    # rc=0
/usr/sbin/eips 22 10 "GUI STOPPED 05:57"    # rc=0
```

`update_to_display: update_mode=PARTIAL, wave_mode=257` が出て、`scripts/dev/shots/05-eips-draw.png` に文字が写った（2 行が同じ行に重なって見える）。`/mnt/us/usbnet/bin/fbink` も存在するが、今回は eips だけで確認。

判定: **OK（GUI 停止中でも直接描ける）**。

## 4. 自動スリープ（idle タイマー）

`lipc-get-prop com.lab126.powerd status` の `Remaining time in this state` を 10 秒あけて 2 回読んだ（待ちなし）。

```text
T1: Remaining time in this state: 584.079520   (05:56:43)
T2: Remaining time in this state: 574.027151   (05:56:53)
```

10 秒で 10.05 秒減った。GUI 停止中も idle タイマーは動いている。

判定: **OK（タイマーは動く）＝ 自動スリープは止まらない**。タイマーが 0 になったときの挙動（スリープ→復帰）は試していない（下記リスク）。

## 5. `start lab126_gui` とブラウザ起動

### start → 純正ホーム

1 回目（step 2 の停止から）:

```text
/sbin/start lab126_gui  -> rc=0, コマンド復帰 0.51 秒
05:57:03  start 発行
05:57:08  framework（cvm）起動
05:58:12  activeAppPid がまだ死んだ 4772
05:58:20 頃 activeAppPid=32445（新しい cvm）
05:58:33  画面はまだ白（06-start-poll-3.png）
05:58:43  純正ホーム表示（06-start-poll-4.png）
```

→ **発行からホームまで約 90〜100 秒**（05:58:33 はまだ白、05:58:43 でホーム）。

2 回目（DONT_START_FRAMEWORK からの復帰）:

```text
06:00:54  start 発行（コマンド復帰 0.44 秒）
06:01:08  cvm pid 4418 起動
06:02:16  activeApp=com.lab126.booklet.home
```

→ **約 82 秒**。

### ブラウザ

```sh
lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=https%3A%2F%2Fexample.com%2F'
```

- `rc=0`、`activeApp=com.lab126.browser` まで **1.49 秒**。
- `mesquite[2075]` のログ: `Load took: 0.9s`、05:59:27〜28 に document loaded。
- 発行の約 4 秒後に `07-browser.png` で example.com が表示された。

判定: **OK**。

## 6. `DONT_START_FRAMEWORK` の効果

`/etc/upstart/framework.conf` の pre-start:

```text
if [ -f /mnt/us/DONT_START_FRAMEWORK ] ; then
    f_log I framework dont_start
    stop
    exit 0
fi
```

手順: `stop lab126_gui`（0.49 秒）→ `touch /mnt/us/DONT_START_FRAMEWORK` → `start lab126_gui`（コマンド復帰 0.31 秒）。

6 秒後の状態:

```text
lab126_gui     start/running
framework      stop/waiting        <- cvm は起動しない
x              start/running, process 2541
pillow         start/running, process 2292
kb             start/running, process 2267
blanket        pid 3046
appmgrd        pid 1899
cvm            []（無し）
```

X / awesome / blanket / appmgrd は `lab126_gui` の子ではないので以前から動いていたもの。pillow と kb は起動した。画面は `08-dont-start-framework.png` — **起動スプラッシュのまま**（プログレスバーが 10% 前後）。`framework_setup` は `start on started framework` なので動かず、スプラッシュの後片付けも来ない。

ブラウザ起動（cvm 無し）:

- 1 回目（既存の mesquite pid 2075 が生きていた）: `rc=0`、0.15 秒で `activeApp=com.lab126.browser`。ログは URL を読みに行っているが、`com.lab126.chromebar` が ServiceUnknown。画面は `09-browser-no-cvm.png` のとおりスプラッシュのまま。
- mesquite を kill してから再試験: **2.18 秒で新しい mesquite（pid 3513）が spawn**、`activeApp=com.lab126.browser / pid 3513`。画面は `10-browser-no-cvm-respawn.png` で、やはりスプラッシュのまま（chromebar が無いためツールバーも出ない）。

判定: **プロセスとしては動くが、画面は使えない → NG（実用不可）**。cvm 無しではスプラッシュが降りず、ブラウザも前面に出ない。

復帰: `stop lab126_gui`（0.18 秒）→ `rm /mnt/us/DONT_START_FRAMEWORK` → `start lab126_gui`。フラグが消えていることを確認してから start した。復帰後は 5 節の 2 回目と同じ（約 82 秒でホーム）。

## 7. 最終状態

```text
/mnt/us/DONT_START_FRAMEWORK : 無し
khome                        : 無し（upstart ジョブも無い）
cvm                          : pid 4418, state S
awesome                      : pid 3045, state S
lab126_gui                   : start/running
framework                    : start/running, process 4071
pillow / kb                  : start/running
appmgrd                      : start/running, process 1899
activeApp                    : com.lab126.booklet.home
powerd state                 : active
```

最終画面: `scripts/dev/shots/12-final.png`（純正ホーム、My Kindle / マイライブラリ）。`lipc-wait-event` の残骸も 0。

判定: **OK**。

## 所要時間のまとめ

| 操作 | 所要時間 | 根拠 |
| --- | --- | --- |
| khome SIGTERM → 終了 | 4 秒 | khome.log 14:53:35→39 |
| `stop lab126_gui` | 0.39 秒（初回）/ 0.49 秒（フラグ前）/ 0.18 秒（framework 停止中） | /proc/uptime 差分 |
| `start lab126_gui` のコマンド復帰 | 0.31〜0.51 秒 | /proc/uptime 差分 |
| `start` → 純正ホーム表示 | 90〜100 秒（1 回目）/ 約 82 秒（2 回目） | kshot と activeApp |
| ブラウザ `activeApp` まで | 1.49 秒 | lipc-get-prop ポーリング |
| ブラウザ表示まで | 約 4 秒 | mesquite ログ + kshot |
| DONT_START でブラウザ spawn（cvm 無し） | 2.18 秒 | pidof ポーリング |
| 電源ボタン → screenSaver（GUI 稼働時のみ） | 2 秒以内（実測 5.7 秒で復帰） | wait-event |

## GUI 停止中に使える / 使えない機能

| 機能 | 判定 | 備考 |
| --- | --- | --- |
| Wi-Fi / SSH | OK | wpa_state=COMPLETED、LAN ping 可 |
| フロントライト | OK | flIntensity と backlight が連動 |
| 電池残量・充電状態 | OK | battLevel / isCharging |
| タッチ入力（event1） | OK | 注入したタップが読める |
| 直接描画（eips / fb0） | OK | eips の文字が画面に出る |
| 自動スリープのタイマー | OK（動く） | 止められない点に注意 |
| 電源ボタン → スリープ / 復帰 | **NG** | `Splash screen is on. Ignoring power button`。goingToScreenSaver が来ない |
| 物理電源ボタン | **未確認**（同じ理由で NG の公算） | event0 の読み手が存在しない |
| 純正ホーム / ステータスバー | NG | cvm が止まるので描かれない（画面は白） |
| ブラウザ（cvm 無し） | NG（画面） | mesquite は起動するがスプラッシュのまま |
| appmgrd の activeApp | 注意 | 停止直後は死んだ pid を返す |

## リスク

1. **再起動監視**: `MAX_RESTARTS=3` / `MAX_REBOOTS=2`。手動 stop/start では増えないが、`lab126_gui_monitor` は `kb` / `framework` / `framework_setup` の**異常終了**で `lab126_gui` を `MONITOR_RESTART=1` で再起動し、3 回で **reboot**、persist が 2 以上で customer service 画面に進む。自作 UI が `kb` や `framework` を巻き込んで落とすと危険。今回は 3 往復でカウンタは空のままだった。
2. **電源ボタン**: GUI 停止中は powerd が「スプラッシュ中」と判断して電源ボタンを無視する。**idle タイマーは動いている**ので、放置すると自動で screenSaver / 深いサスペンドに入り、電源ボタンで戻れない可能性がある。停止中の長時間放置は避ける。今回のスパイクでは 10 秒を超えるスリープは一度も作っていない。
3. **復帰時間**: `start` からホームまで 82〜100 秒。`framework_setup` の `FrameworkStarted` 待ちは **105 秒**で、超えると cvm に SIGQUIT + dump-stack して失敗扱いになる。コールドスタートが遅いとこの線に近い。
4. **appmgrd の残骸**: `stop lab126_gui` 直後も `activeApp=com.lab126.booklet.home` / 死んだ pid を返す。停止中にこのプロパティで状態判定しない。
5. **画面**: 停止すると framebuffer は白になる（直前の画面は残らない）。自前 UI は必ず全面を描く必要がある。
6. **DONT_START_FRAMEWORK**: cvm を止められるがスプラッシュが降りない。単独では「画面のない端末」になる。フラグを置いたままスリープ試験をしないこと（今回は実施していない）。

## 未確認

- 物理の電源ボタンを実際に押したときの挙動（押せないため）。event0 の読み手が誰か、も特定できていない。
- idle タイマーが 0 になったとき、GUI 停止中に screenSaver / SUSPENDED へ入るか、そこから復帰できるか。
- インターネット宛の通信（ICMP は遮断されていた。HTTP は未試験）。
- `DONT_START_FRAMEWORK` + 自前 UI がスプラッシュを消せるか（`splash_cleanup` を誰が呼ぶか）。
