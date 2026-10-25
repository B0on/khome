# khome 新方式 前半（cvm 無し + fakekaf）実機確認

2026-10-23。Paperwhite 2（758×1024）/ FW 5.12.2.1.1 / `ssh kindle`（<kindle-ip>, root）。
コマンドはすべて先頭に `export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin DISPLAY=:0;` を付けて実行した。
時刻は端末の UTC（JST は +9）。画面は `scripts/dev/kshot.sh`、タップは `scripts/dev/ktap.sh`、画像は `scripts/dev/shots/`（git 管理外）。

制約として守ったこと: `mntroot rw` / rootfs への書き込み / upstart ジョブの追加 / 自動起動 / reboot / `fw_setenv` / `dd` / `com.lab126.browser go` / `preventScreenSaver` は未使用。書き込みは `/mnt/us/khome` と `/mnt/us/DONT_START_FRAMEWORK`、`/tmp` のみ。スクリーンセーバー滞在は最長 3.7 秒。

---

## 結論

- cvm 無し + fakekaf で、起動（a）・全画面ブラウザ（b）・SIGUSR1 でのブラウザ終了とホーム復帰（c）・ホーム／ブラウザ表示中のスリープ復帰（d）・SIGTERM での純正復帰（e）がすべて通った。
- 起動からホームまで約 10 秒、純正復帰は SIGTERM から画面がホームになるまで約 90〜105 秒（130 秒以内）。
- `DONT_START_FRAMEWORK` は framework の pre-start が読んだ直後（`/var/log/messages` の `framework:dont_start` を確認）に消している。フラグを消した時点で cvm は動かず、再起動しても純正で立ち上がる。
- ブラウザ終了の検知は **プロセス（mesquite）の消滅**を正とした。appmgrd の `processExited` は届くが、**前回 kill した分が 39 秒遅れて次回起動後に届く**ことがあり、現在のブラウザの終了とは限らない（下記 5 節）。

---

## 1. ビルドと配置

```sh
cd launcher
./build.sh test   # 37 unit + 19 render, all passed
./build.sh arm    # build/arm/khome
cd ..
launcher/tools/deploy.sh   # khome, fakekaf, khome.sh を /mnt/us/khome へ
```

deploy 後の端末:

```text
-rwxrwxrwx 1 root root 1163136 /mnt/us/khome/bin/fakekaf
-rwxrwxrwx 1 root root 2335320 /mnt/us/khome/bin/khome
-rwxrwxrwx 1 root root    5350 /mnt/us/khome/khome.sh
```

`tabs.txt` は新しい書式（1 行目 `active=N`、以降 1 行 1 URL）で置いた。今回の確認では `active=1`（2 件目）を開かせて、パーサと active の解釈を確かめた。

```sh
printf 'active=1\nhttps://www.google.co.jp/\nhttps://example.com/\n' > /mnt/us/khome/tabs.txt
```

---

## 2. a. 起動とホーム表示

```sh
nohup /mnt/us/khome/khome.sh >/tmp/khome-sh.out 2>&1 &
# 30 秒後
```

```text
cvm=[] fakekaf=[31376] khome=[31388] flag=[]
framework  stop/waiting
frameworkStarted=1
blanket load = screensaver langpicker blankwindow usb
```

`/mnt/us/khome/khome.log`:

```text
16:09:50 khome: start
16:09:51 khome: touch /dev/input/event1, fb 758x1024
16:09:51 powerd watch started
16:09:51 appmgrd watch started
16:09:51 HOME + start -> HOME
16:09:51   action grab
16:09:51 touch: EVIOCGRAB 1
16:09:51   action draw-home
```

画面: `70-home.png`。SSID は `WIFI / <SSID>`、電池 75%。SSID は `wpa_cli -i wlan0 status` の `ssid=` 行から取り、`wpa_state=COMPLETED` のときだけ出している。

判定: **OK**。

---

## 3. b. ブラウザを全画面で開く

```sh
scripts/dev/ktap.sh 452 520
```

ログ:

```text
16:10:27 tap 452,520 -> design 639,735
16:10:27 tap action app_browser
16:10:27 HOME + tap-browser -> BROWSER
16:10:27   action ungrab
16:10:27 touch: EVIOCGRAB 0
16:10:27   action start-browser
16:10:27 browser: opening https://example.com/
16:10:27 browser: start requested
```

```text
# lipc-get-prop com.lab126.appmgrd activeApp
com.lab126.browser
# pidof mesquite
31702
# xwininfo -id 0x1200021
Absolute upper-left X: 0 / Y: 0
Width: 758 / Height: 1024 / Map State: IsViewable
```

画面: `71-browser.png`（example.com が全面表示）。`active=1` の 2 件目が開いており、tabs.txt の解釈も確認できた。

判定: **OK**。

---

## 4. c. SIGUSR1 でブラウザを閉じてホームへ

```sh
kill -USR1 31388
```

ログ:

```text
16:10:46 BROWSER + browser-close -> HOME
16:10:46   action close-browser
16:10:46 browser: kill 1
16:10:46 browser: exited after about 100 ms
16:10:47 browser: alert shown, tapping close
16:10:48 touch: injected tap 597,602
16:10:52 touch: injected tap 598,601
16:10:52 touch: EVIOCGRAB 1
```

```text
# 10 秒後
mesquite=[] alerts=0 activeApp=[]
```

- `kill` は文字列プロパティである。`lipc-set-prop -i com.lab126.browser kill 1` は `lipcErrNoSuchProperty`（rc=8）で失敗する。`lipc_set_str` で送ること。
- ダイアログの「閉じる」は 1 回目のタップでは消えず、500 ms 後の 2 回目（+1 px）で消えた。`browser_close()` は最大 3 回まで 500 ms 間隔で試す。
- 連続で同じ座標を注入すると input core が ABS の重複を落とし、タップが (0,0) になる。注入側で 1 px のジッタを付けている（実測: 同一座標の再タップが 0,0 として届いた）。

画面: `72-home-after-close.png`（ダイアログ無し、ホーム全面）。

判定: **OK**。

---

## 5. d. スリープ復帰

### 5.1 ホーム表示中

```sh
lipc-set-prop -i com.lab126.powerd powerButton 1   # 16:11:06
sleep 3
lipc-set-prop -i com.lab126.powerd powerButton 1   # 16:11:09
```

```text
[07:11:06.511701] goingToScreenSaver 2
[07:11:09.681159] outOfScreenSaver 1
[07:11:09.702993] exitingScreenSaver
```

```text
16:11:09 powerd: outOfScreenSaver
16:11:09 HOME + outOfScreenSaver -> HOME
16:11:09   action schedule-redraw
16:11:10 HOME + defer-redraw -> HOME
16:11:10   action draw-home
```

画面: `73-home-after-wake.png`。スクリーンセーバー滞在は約 3.2 秒。

判定: **OK（outOfScreenSaver の約 500 ms 後に全面描き直し）**。

### 5.2 ブラウザ表示中

```sh
# ブラウザを開いた後、同じく powerButton を 2 回
```

```text
[07:11:33.041953] goingToScreenSaver 2
[07:11:36.221243] outOfScreenSaver 1
[07:11:36.257716] exitingScreenSaver
```

```text
16:11:36 powerd: outOfScreenSaver
16:11:36 BROWSER + outOfScreenSaver -> BROWSER
```

`mesquite=31916` のまま、ウィンドウは `758x1024+0+0 / IsViewable`。khome は何も描かず、X がブラウザを描き直した。画面: `74-browser-after-wake.png`。

判定: **OK**。

### 5.3 appmgrd の終了イベントについて

`lipc-wait-event -m -t com.lab126.appmgrd '*'` で観測した実イベントは `processExited`（引数 `com.lab126.browser`）だった。ただし次のログのとおり、06:56:52 に送られたイベントが khome に 06:57:31（次のブラウザ起動後）まで届かなかった。

```text
06:56:52 appmgrd: processExited ... fparam=com.lab126.browser:Event sent
06:57:31 khome: appmgrd: processExited for the browser   <- 39 秒遅れ
```

そのため状態機械はイベントだけを信用せず、`/proc/*/comm` の `mesquite`（1 秒間隔）を正とし、イベントは「プロセスが実際に消えているとき」だけ使う。今回の c では kill 後 100 ms でプロセス消滅を検知した。

---

## 6. e. 純正へ戻す

ブラウザを開いたまま `khome.sh` に SIGTERM を送った（ブラウザの kill 経路も通す）。

```sh
kill -TERM 30913   # 16:11:50
```

3 秒後:

```text
mesquite=[] fakekaf=[] khome=[] flag=[]
```

以後のポーリング:

```text
07:12:04 cvm=32715 titleBar=0 blanket=screensaver langpicker blankwindow usb splash
07:12:36 cvm=32715 titleBar=0 ... splash
07:13:10 cvm=32715 titleBar=0 ...（splash が消えた）
07:13:22 cvm=32715 titleBar=1 ...（ホームのウィンドウが viewable）
```

最終状態:

```text
cvm=32715  fakekaf=[]  khome=[]  flag=[]
framework start/running, process 32368
activeApp=com.lab126.booklet.home
com.lab126.kaf frameworkStarted=1（本物の kaf）
blanket load = screensaver langpicker blankwindow usb（splash 無し）
/var/local/upstart/ は空
```

画面: `75-stock-final.png`。`titleBar=1` になった直後の kshot はまだ白かった（ホームの描画中）。約 12 秒後の再取得で My Kindle / マイライブラリが出た。SIGTERM から画面がホームになるまで約 90〜105 秒で、130 秒以内。

判定: **OK**。

---

## 7. `DONT_START_FRAMEWORK` を消してよい時点の根拠

読んだファイル:

- `/etc/upstart/framework.conf:1` — `start on contentpack_font_ready`
- `/etc/upstart/framework.conf:8-16` — pre-start。`/mnt/us/DONT_START_FRAMEWORK` があれば `f_log I framework dont_start`（12 行目）を出して `stop`。**フラグを読むのはここだけ**（`/etc/upstart` 全体を grep して他に無いことを確認）。
- `/etc/upstart/contentpack_fonts_dependency.conf:1` — `start on started lab126_gui`
- `/etc/upstart/contentpack_fonts_dependency.conf:3` — `emits contentpack_font_ready`
- `/etc/upstart/contentpack_fonts_dependency.conf:13-21` — `lipc-get-prop com.lab126.contentpackd fontsMountedState` と `lipc-wait-event -s 2` を最大 5 回。`stop lab126_gui` は contentpackd も止めるため、このジョブは `start` 後 最大約 10 秒かかる。

したがって `start lab126_gui` の直後は framework が **前回の stop のまま stop/waiting** であり、この時点でフラグを消すと、約 10 秒後に `contentpack_font_ready` が来きたときに cvm が起動してしまう。khome.sh は pre-start 自身のログ行（`framework:dont_start`）が増え、かつ `initctl status framework` が `stop/waiting` になるまで待ってから消す。実測:

```text
261023:070949 system: I framework:dont_start::
# 16:09:50 に khome 起動、16:09:51 にホーム描画
```

フラグ削除後も `pidof cvm` が空であることを確認している。消した後に framework が起動する経路は無い（この起動では framework ジョブは stop/waiting のまま）。

---

## 8. 変更点（今回の範囲）

- `launcher/src/main.c` — cvm 共存用の own/release・pillow・サービス stop/start・fb dump・HOME_APP_STARTED・電源キー監視を削除。`GRAB`/`UNGRAB` と `DRAW_HOME` の単純な状態機械に接続。ブラウザは `appmgrd start 'app://com.lab126.browser?view=<encode>'`、終了は `kill 1` + ダイアログのタップ。SIGUSR1 で `browser_close()`。SSID は `wpa_cli`。
- `launcher/src/state.h` / `state.c` — 状態は HOME / BROWSER / KOREADER。イベントは outOfScreenSaver、browser-exit、browser-close など。`khome_step` は純関数のまま。
- `launcher/src/input.c` / `input.h` — event0（電源キー）の open/poll を削除。
- `kindle/khome/khome.sh` — stop → フラグ → start → フラグ読取の確認 → フラグ削除 → blanket/appmgrd 待ち → fakekaf → khome。終了時はブラウザ kill → fakekaf TERM → stop → フラグ削除 → start。
- `launcher/tools/deploy.sh` — fakekaf をビルドして `/mnt/us/khome/bin/fakekaf` へ送る。
- `launcher/src/tabs.c` を build.sh の 4 か所（HOST_SRC / ARM_SRC / ARM_KHOME_SRC / TEST_SRC）に追加。`launcher/src/url.c` / `url.h` は削除し、エンコードは `tabs_encode_component` に移した。
- `launcher/tests/test_state.c` — 消した仕組みを確かめていた次のテストを削除: `state.browser_app_started_schedules_take_back`、`state.koreader_exit_schedules_take_back`、`state.own_ignores_booklet_home`、`state.duplicate_events_are_idempotent`。残した 3 本は新しいアクション名に合わせた。

---

## 9. 未確認・リスク

1. **深いサスペンドからの復帰は未確認。** スクリーンセーバー滞在を最長 3.9 秒に抑えたため、t2 タイマーによる suspend は試していない。
2. **物理の電源ボタンは未確認。** 確認したのは `powerd powerButton 1` のみ。
3. **ブラウザのクラッシュ終了**は未確認。プロセス監視で 1 秒以内にホームへ戻る実装だが、そのとき pillow のエラーダイアログが残る可能性がある（SIGUSR1 経路でのみダイアログを閉じている）。
4. **`appmgrd` の `activeApp` は cvm 無しでは古い値（`com.lab126.booklet.home`）を返すことがある。** 状態判定に使っていない。
5. **`tabs.txt` が 8 KB を超える場合**は先頭 8 KB だけをパースする（8 タブ × 1024 バイト + ヘッダで足りる想定）。
6. **再起動監視**: 手動の stop/start ではカウンタは増えない（`no-framework-spike.md` 0 節）。今回も `/var/local/upstart/` は空のまま。
7. `/mnt/us/mesquite_*.core` は前回のものが残ったままで、今回の作業では増えていない（`go` を使っていない）。
