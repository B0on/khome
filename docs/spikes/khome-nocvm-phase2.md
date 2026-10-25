# khome 新方式 後半（タブ一覧シート）実機確認

2026-10-23。Paperwhite 2（758×1024）/ FW 5.12.2.1.1 / `ssh kindle`（<kindle-ip>, root）。
コマンドはすべて先頭に `export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin DISPLAY=:0;` を付けて実行した。
khome のログ時刻は JST（`TZ=JST-9`）。画面は `scripts/dev/kshot.sh`、タップは `scripts/dev/ktap.sh`、画像は `scripts/dev/shots/`（git 管理外）。
制約として `mntroot rw` / rootfs への書き込み / upstart ジョブ / reboot / `fw_setenv` / `dd` / `com.lab126.browser go` / `preventScreenSaver` は未使用。書き込みは `/mnt/us/khome` と `/tmp` のみ。スクリーンセーバー滞在は 3 秒。

## 結論

- BROWSER 中に event1 を grab せず読み、上端からの下スワイプで SHEET に入り、シートを `/dev/fb0` の上端だけに描いて e-ink を部分更新できた（a）。
- sheet_new / sheet_tab / sheet_dismiss / sheet_home / スリープ復帰が実機で通った（b〜f）。
- ブラウザの現在 URL を読める LIPC プロパティとイベントは**無い**。`lipc-probe -v com.lab126.browser` に URL/タイトル系は無く、遷移中に `lipc-wait-event` でも何も流れない。したがってタブの URL は「タブを開いた時点のもの」を使う（3 節）。
- シートを閉じた後の再描画は、X に Expose を起こす補助バイナリ `xexpose` で確実に戻せた。X はシートの下を自分では描き直さない（4 節）。
- スワイプ注入は 7 回中 2 回認識されなかった。認識されないときは `touch release without tap` だけが出る。座標を変えて再注入すると通る（6 節）。

---

## 1. 実装の形

### 状態とイベント

`launcher/src/state.h` に `KHOME_STATE_SHEET` を足した。`khome_step` は純関数のまま。

| 状態 + イベント | 次状態 | アクション |
| --- | --- | --- |
| BROWSER + sheet-pull | SHEET | grab, draw-sheet |
| SHEET + sheet-dismiss / シート外タップ | BROWSER | ungrab, hide-sheet |
| SHEET + sheet-back | BROWSER | browser-back, ungrab, hide-sheet |
| SHEET + sheet-home | HOME | close-browser |
| SHEET + sheet-new | BROWSER | sheet-new, ungrab, hide-sheet |
| SHEET + sheet-tab(i) | BROWSER | sheet-tab, ungrab, hide-sheet |
| SHEET + sheet-close(i)（背景タブ） | SHEET | sheet-close（再描画） |
| SHEET + sheet-close-active(i) | BROWSER | sheet-close-active, ungrab, hide-sheet |
| SHEET + sheet-close-last(i) | HOME | sheet-close-last（タブを閉じて browser_close） |
| SHEET + outOfScreenSaver | BROWSER | ungrab（X がブラウザを描き直す） |
| SHEET + browser-exit / browser-close | HOME | grab + draw-home / close-browser |

- 添字 `i` は main.c の `sheet_index` に置き、`handle_event` の直前に設定する。イベントに添字を持たせると `khome_step` の引数が増えて純関数でなくなるため。
- `sheet_close` の分岐（最後の 1 枚か、表示中のタブか、背景タブか）は main の `on_sheet_tap` がタブ一覧を見てイベントを選ぶ。状態機械は件数を持たない。
- タブを変えたときは `tabs_format` → `/mnt/us/khome/tabs.txt.tmp` → `rename` で書く。`tabs.txt` の書式は位相 1 のまま（`active=N` + 1 行 1 URL）。

### 描画と入力

- シートは `canvas_clear` → `draw_tab_sheet` → `fb_blit_rect(0, 0, 758, sheet_h)` → `fb_refresh_rect`。`fb_blit_rect` を足したので、シートの下のブラウザの画素は /dev/fb0 上で壊さない。
- BROWSER 中は `input_poll_gesture`（`gesture_handle_result`）で読み、`GESTURE_RESULT_SHEET_PULL` だけを拾う。タップは従来どおり無視する。SHEET では grab するのでシートのタップだけが khome に届く。
- `grab_touch` は grab の向きが変わるたびに event1 を drain して `gesture_init` する。前の状態のイベントが新しい状態のジェスチャにならないようにするため。

---

## 2. 実機の結果（a〜f）

起動は `nohup /mnt/us/khome/khome.sh >/tmp/khome-sh.out 2>&1 &`。cvm 無し、fakekaf あり、フラグ無しでホームが出た。

### a. 上端スワイプでシート（`80-sheet.png`）

```sh
scripts/dev/ktap.sh 452 520          # ホームのブラウザ
sleep 6
cat /tmp/swipe.bin > /dev/input/event1   # (379,10) -> (379,310)、12 点、x を 1px ずつ揺らす
```

```text
16:23:42 touch press
16:23:42 BROWSER + sheet-pull -> SHEET
16:23:42   action grab
16:23:42 touch: EVIOCGRAB 1
16:23:42   action draw-sheet
```

`80-sheet.png` は上端に「戻る / ホーム / 新しいタブ」、URL 欄、タブ 2 行（example.com と google.co.jp）。前回の確認で残っていた `tabs.txt` が 2 件だったため。シートの下端は design 500（device 354px）。判定 **OK**。

スワイプは grab していないのでブラウザにも届き、ページは約 300px スクロールした。シートの下が白いのはそのため（ページの先頭が上に消えた）。シート自体は正しく出ている。

### b. sheet_new と 2 タブ（`81-new-tab.png`, `82-two-tabs.png`）

「新しいタブ」は device (605,71)。

```text
16:23:51 tap action sheet_new
16:23:51 SHEET + sheet-new -> BROWSER
16:23:51 browser: opening https://www.google.co.jp/
16:23:51 sheet: xexpose exposed
active=2
https://example.com/
https://www.google.co.jp/
https://www.google.co.jp/   <- この時点の tabs.txt（下記の理由で 3 件）
```

`81-new-tab.png` は Google のページで、シートは閉じている。判定 **OK**。

この確認では、直前に旧バイナリで作った 2 タブが `tabs.txt` に残っていたため、sheet_new でもう 1 枚増えて 3 件になった。82 の前に `tabs.txt` を 2 件（active=1）に置き直し、シートを出し直した。この置き直しは `DRAW_SHEET` がシートを出すたびに `tabs.txt` を読み直すため、khome を再起動せずに反映できた。

```text
16:24:06 BROWSER + sheet-pull -> SHEET
```

`82-two-tabs.png` は example.com と google.co.jp の 2 行、active は google。判定 **OK**。

### c. 1 つ目のタブへ（`83-switch.png`）

device (356,216) は 1 行目のタブ。

```text
16:24:15 tap action sheet_tab
16:24:15 SHEET + sheet-tab -> BROWSER
16:24:15 browser: opening https://example.com/
16:24:16 sheet: xexpose exposed
active=0
```

`83-switch.png` は example.com（スワイプでスクロールした位置）。判定 **OK**。

### d. sheet_dismiss（`84-dismissed.png`）

2 タブのシートの下端ハンドルは design 468..500（device 331..354）。device (380,340)。

```text
16:24:33 tap action sheet_dismiss
16:24:33 SHEET + sheet-dismiss -> BROWSER
16:24:33 sheet: xexpose exposed
```

`84-dismissed.png` はシートの跡が無く、ブラウザのページだけ。判定 **OK**。

### e. sheet_home と再オープン（`85-home.png`, `86-reopen.png`）

```text
16:25:33 tap action sheet_home
16:25:33 SHEET + sheet-home -> HOME
16:25:33 browser: kill 1
16:25:33 browser: exited after about 100 ms
16:25:34 browser: alert shown, tapping close
```

`85-home.png` は khome のホーム。`activeApp` は空、mesquite は消滅。続けてブラウザを開くと `active=0` の example.com が開いた。

```text
16:25:50 HOME + tap-browser -> BROWSER
16:25:50 browser: opening https://example.com/
```

`86-reopen.png` は example.com。判定 **OK**。

### f. SHEET 中のスリープ復帰（`87-wake.png`）

```sh
lipc-set-prop -i com.lab126.powerd powerButton 1
sleep 3
lipc-set-prop -i com.lab126.powerd powerButton 1
```

```text
16:26:14 powerd: outOfScreenSaver
16:26:14 SHEET + outOfScreenSaver -> BROWSER
16:26:14   action ungrab
16:26:14 touch: EVIOCGRAB 0
```

`87-wake.png` はシートが消えて example.com。X がブラウザを描き直した。判定 **OK**。

---

## 3. ブラウザの現在 URL を khome が知る方法（調査）

### `lipc-probe -v com.lab126.browser`

ブラウザ表示中の全プロパティ。URL / タイトル / 履歴に相当するものは無い。

```text
w  Str processRestart / pause / load / kill / unload / run / go
w  Str searchSelection / searchBarButtonSelected / switchViewMode
w  Str keyboardGetSurround / keyboardCommit / keyboardDelete / keyboardReplace / keyboardSetPreeditString
w  Str wifiPopupDone / wirelessOn / captivePortalResponse / demoWifiNotAvailableAlertResponse
rw Str logLevel / logMask
r  Str ready [0]
```

`lipc-get-prop com.lab126.browser url` は `lipcErrNoSuchProperty`（rc=8）。`currentUrl` / `location` も同じ（khome のログに `browser: current URL from ...` が出ないことで確認）。

`com.lab126.appmgrd` 側も `activeApp` / `activeAppPid` / `start` / `goresult` / `loadresult` などで、現在 URL は持っていない。

### `lipc-wait-event`

遷移を起こしながら `lipc-wait-event -m -t -s 7 com.lab126.browser '*'` を 7 秒待ったが、**1 行も出なかった**。

```sh
lipc-wait-event -m -t -s 7 com.lab126.browser "*" >/tmp/ev.txt &
lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=https%3A%2F%2Fexample.com%2F%3Fkhome_probe%3D1'
# /tmp/ev.txt は空
```

X のウィンドウ名も `L:A_N:application_PC:TS_ID:com.lab126.browser` で、ページタイトルや URL は入っていない（`xwininfo -root -tree`）。

### 結論

**取れない。** `tabs_set_active_url` で更新する経路は実装したが（`url` / `currentUrl` / `location` を読み、`http(s)://` で始まるときだけ採用）、この FW では一度も発火しない。タブの URL はタブを開いた時点のものを使う。ページ内リンクで移動した後に別タブへ切り替えると、元のタブの URL は古いままになる。これは既知の制限として残す。

---

## 4. シートを閉じた後の再描画（比較）

khome は /dev/fb0 に直接描くので、X はシートの下にダメージを持たない。実際、`xexpose` が失敗したとき（16:19:23）はシートの画素がそのまま残り、ブラウザは戻らなかった（`80c.png`）。したがって「何もしない」では駄目。

### (a) 端末の X 再描画ツール

`xrefresh` も `xdotool` も無い（`command -v` で空）。`/usr/bin` にあるのは `xset` / `xev` / `xwd` / `xwininfo` / `xrandr` 程度で、矩形の再描画を起こすものは無い。`xev` は自分のウィンドウを作るが、awesome にタイルされ、確実にブラウザの上へ出せる保証が無いので採用しなかった。

### (b) Expose を起こす補助バイナリ（採用）

`kindle/xexpose/xexpose.c`。引数 `x y w h` で override-redirect のウィンドウをその矩形に 60ms だけ map し、unmap / destroy する。下にいたブラウザに Expose が飛び、mesquite が /dev/fb0 へ描き直す。khome はその後 `fb_refresh_rect` で同じ矩形を e-ink に送る。

```sh
/mnt/us/khome/bin/xexpose 0 0 758 277
# exposed   (rc=0)
```

`84-dismissed.png` と `87-wake.png` でシートの跡が残らないことを確認した。所要は 60ms の待ち + X の往復で、体感は 0.1〜0.2 秒。

ビルドは `kindle/xexpose/build.sh`。`kindle/fakekaf/build.sh` と同じ `zig cc -target arm-linux-gnueabi.2.20 -mfloat-abi=soft` と `kindle/fakekaf/sysroot` を使う。Xlib はヘッダもリンクも使わず `dlopen("libX11.so.6")` + `dlsym` で呼ぶ。

**踏んだ穴**: 最初の版は `fprintf(stderr, ...)` と `printf` を使っていた。その binary は端末の glibc 2.20 の ld.so で main に入る前に SIGSEGV した（`si_addr=0x10054`）。`.rel.dyn` に `R_ARM_RELATIVE` が 1 個（`stderr` 用、symbol index 4）入っており、この ld.so がそれを処理できないため。stdout/stderr を参照せず `write(2, ...)` に変えたら `.rel.dyn` が消え、動いた。fakekaf が動くのは `.rel.dyn` を持たないため。補助バイナリを足すときは同じ罠に注意。

### (c) ページ再読み込み

最後の手段としては使っていない。`appmgrd start` で同じ URL を開き直せば再描画は起きるが、スクロール位置とページ状態が失われる。xexpose が rc=0 で戻ることを確認したので不要。

---

## 5. 変更点（今回の範囲）

- `launcher/src/state.h` / `state.c` — `KHOME_STATE_SHEET`、sheet-* イベントとアクション、遷移表。
- `launcher/src/main.c` — SHEET の描画（上端だけ blit + 部分更新）、シートのタップ配送、タブの永続化（tmp + rename）、sheet_new / tab / close / close-active / close-last、browser-back、xexpose 呼び出し、SHEET 中のブラウザ終了監視。
- `launcher/src/input.c` / `input.h` — `input_poll_gesture`（`gesture_handle_result` を返す）。`input_poll` はそのラッパ。
- `launcher/src/fb.c` / `fb.h` — `fb_blit_rect`。
- `kindle/xexpose/xexpose.c` / `build.sh` — X に Expose を起こす補助バイナリ。
- `launcher/tools/deploy.sh` — xexpose をビルドして `/mnt/us/khome/bin/xexpose` へ送る。
- 既存テストは状態名を足しただけで、削除・弱体化はしていない（37 unit + 19 render が通る）。

---

## 6. 純正へ戻す

`khome.sh` に SIGTERM を送った（16:26:48）。約 2 分後:

```text
cvm=4381  fakekaf=[]  khome=[]  flag=[]
framework start/running, process 4034
lab126_gui start/running
activeApp=com.lab126.booklet.home
com.lab126.kaf frameworkStarted=1
blanket load = screensaver langpicker blankwindow usb   (splash 無し)
```

`88-stock.png` は My Kindle / マイライブラリ。判定 **OK**。`mesquite_*.core` は前回のものがライブラリに残ったままで、今回増やしていない。

## 7. 未確認・リスク

1. **スワイプ注入が 7 回中 2 回認識されなかった。** 失敗時は `touch press` の後に `touch release without tap` だけが出る。同じバイト列を送り直すと通る。evdev の MT スロットの前状態（直前のタップの release）に依存している可能性があるが、原因は特定していない。実機の指では未確認。
2. **シート表示中のスワイプはブラウザにも届く。** BROWSER では grab していないため、上端スワイプでページが約 300px スクロールする。シートを出す操作とページ操作が同時に起きる。実用上は「上端 40px からの下スワイプ」だけを khome が拾い、ブラウザ側のスクロールを抑える必要がある（今回は未対処）。
3. **現在 URL は取れない**（3 節）。リンク遷移後のタブ切り替えで URL が古くなる。
4. **sheet_new はシートを閉じて BROWSER に戻る**実装にした。完了条件 b の「新しいタブでページが出る」に合わせた。仕様文には「シートを閉じる」と明記されていないため、解釈をここに残す。sheet_tab も同じく閉じる。背景タブの close だけ SHEET に留まる。
5. **sheet_close の表示中タブ**は次の active を開いてシートを閉じる。最後の 1 枚は `browser_close()` で HOME。この 2 経路は今回の a〜f では操作していない（コードとビルドのみ）。
6. **タブが 0 個のときのシート**は描けるが、`sheet_new` 以外に復帰手段が無い。今回は 0 個のシートを出していない。
7. **xexpose は 60ms の override-redirect ウィンドウを出す。** 画面の一瞬のちらつきは e-ink ではほぼ見えないが、X のイベントログにはウィンドウの map/unmap が残る。
8. スリープ復帰は `powerButton` プロパティのみ。物理ボタンは未確認（位相 1 と同じ）。
9. `/mnt/us/mesquite_*.core` は今回増やしていない（`go` を使っていない）。
10. **khome.sh を再起動できないことがある。** khome.sh は起動時に fd 9 で `khome.pid` を flock し、fakekaf はその fd を継承したまま動き続ける。fakekaf が生きている間に khome.sh を落として起動し直すと `another launcher holds /mnt/us/khome/khome.pid` で終了する。今回は開発中に fakekaf を一度止めて回避した。通常の SIGTERM 復帰では restore が fakekaf を止めるので問題にならないが、wrapper の再起動を許すなら fd 9 を fakekaf に渡さない修正が要る（今回の範囲外）。

---

## 8. 追試（2026-10-23 16:30–17:10 JST）

7 節の 2（ブラウザ側のスクロール）と 10（wrapper の再起動）に手を入れ、未操作だった sheet_close の 2 経路とスワイプ注入を実機で追試した。時刻は特記しない限り JST（UTC+9）。

### 8.1 上端の入力帯（xband）

`kindle/xband/` を追加した。`kindle/xexpose` と同じく Xlib を `dlopen("libX11.so.6")` し、stdio のグローバルを参照しない（`write(2, ...)` だけ）。ビルドは `kindle/xband/build.sh`（zig cc, `arm-linux-gnueabi.2.20`, soft-float, `-ldl`）。生成物の `.rel.dyn` は無く、R_ARM_RELATIVE を含まない（`objdump` 相当でセクションを確認）。

- ウィンドウは depth 0 / class InputOnly の override-redirect。矩形は `0,0 758x41`。`GESTURE_SHEET_EDGE_PX = 40` なので、khome が帯と判定する `start_y <= 40`（0..40）を 41px で覆う。
- `ButtonPressMask | ButtonReleaseMask | ButtonMotionMask` を選び、押下で X の暗黙グラブがこのウィンドウに付くようにした。押下後の移動と release はここに届き、ブラウザには渡らない。キューは 100ms ごとに `XPending` / `XNextEvent` で捨てる。
- 100ms ごとに `XRaiseWindow` する。ブラウザのウィンドウは khome が起動した後に map されるので、これをしないと帯が下に回る。100ms 周期なら map 後すぐ戻る。
- ライフサイクルは `launcher/src/main.c` の `sync_band()`。BROWSER または SHEET に入るときに起動し、HOME に戻るとき（browser-exit、sheet-home、sheet-close-last など）と khome 終了時に止める。BROWSER ↔ SHEET では止めない。親が死んだ場合は xband 自身が `getppid() == 1` を 100ms 以内に検知して終了する。
- fd 9 は xband 側でも exec 前に閉じる（8.3）。

実機での確認:

```text
$ xwininfo -root -tree
0x1200001 (has no name): ()  758x41+0+0  +0+0  MapState=IsViewable   <- xband
0x1400021 "L:A_N:..._ID:com.lab126.browser": (...)  758x1024+0+0  +0+0  MapState=IsViewable
```

xwininfo の子の並びで xband がブラウザより上にいた。ブラウザを開いた後に帯が下へ回ることはなかった。

上端スワイプ（`/tmp/swipe.bin`、device (379,10) → (379,310)、12 点）:

```text
16:34:33 touch press
16:34:33 BROWSER + sheet-pull -> SHEET
16:34:33   action grab
16:34:33 touch: EVIOCGRAB 1
16:34:33   action draw-sheet
```

シートを出す前（`90-browser-before.png`）と、ハンドルをタップして閉じた後（`92-after-dismiss.png`、device (380,340)）の全面差は **0 画素**（758x1024 全体、しきい値 8）。シート下（y>=354）だけでなく上端も含めて 1 画素も変わっていない。つまりページはスクロールしていない。`91-sheet-band.png` は帯の下にシートが出ている。

帯より下の入力は塞いでいない。xband のウィンドウは 41px で、それより下に X ウィンドウを追加していない。帯の外から始まるスワイプ（(379,600) → (379,300)）は khome に `touch press` として届き、sheet-pull にはならなかった（`start_y > 40`）。ただしこのとき example.com は内容が短く、ページの画素は動かなかった。**「帯より下のスワイプでブラウザがスクロールする」ことは画素では確認できていない**（下記 8.7）。

### 8.2 cvm のコアダンプ 2 件（cvm_12691 / cvm_15020）

`/mnt/us` に新しい cvm のコアが 2 つできた。

```text
-rwxrwxrwx 88923260 2026-10-23 07:46:44 UTC  cvm_12691_001-...core
-rwxrwxrwx 88952076 2026-10-23 07:51:29 UTC  cvm_15020_001-...core
```

同時刻のクラッシュレポートもできている（`/mnt/us/documents/cvm_12691_..._crash_Oct_23_07.46.46_2026.txt` / `..._07.51.30_2026.txt`）。コアは消していない。

`/var/log/messages` では 15020 の方が残っている:

```text
261023:075129 system: I dumpstack:cvm:pid=15020: Saved corefile /mnt/us/cvm_15020_...core.part
261023:075129 system: I dump-stack:dumpstack:cvm:pid=15020:Completed core dump generation
261023:075138 root: I dmcc:OK:Created .../cvm_15020_..._crash_Oct_23_07.51.30_2026.txt ...
```

`dumpstack` は JVM の SIGSEGV ではなく、**応答しなくなった cvm を dumpstackd が snapshot したもの**。12691 のレポートにも同じ `dumpstack:cvm:pid=12691` の行が残っている。12691 の 07:46 台の `/var/log/messages` は、07:49:02-03 に syslog-ng が SIGHUP で再起動した際に tinyrot が古い messages を消したため現物が残っていない。時刻はコアとレポートの mtime、および作業ログで確定した。

作業との突き合わせ:

| 時刻 (UTC) | 出来事 |
| --- | --- |
| 07:42:35 | 最初の khome.sh に SIGTERM。restore が `stop lab126_gui` → `start lab126_gui`。 |
| 07:43:0x | KOReader（16:41:11 にマンガアイコンのタップで起動していた）の script と reader.lua を TERM/KILL。 |
| ~07:44:45 | 純正に戻すため `stop lab126_gui` → `start lab126_gui`。cvm 12691 が起動。 |
| 07:46:44 | cvm 12691 のコア。この間 khome / fakekaf / xband は動いていない。 |
| 07:49:03-04 | lab126_gui が（こちらが開始したものではない）再起動。cvm 15020 が起動。 |
| 07:51:29 | cvm 15020 のコア。この間も khome / xband は動いていない。 |
| 07:53:5x | `stop lab126_gui; stop x; start x; start lab126_gui` で X ごと再起動。cvm 16544 は 07:55:51 にホームまで到達、コアなし。 |

判断:

- **帯ウィンドウと xexpose は原因ではない。** 2 回のダンプ時点で khome は終了済み（07:42:35）で、xband のプロセスも 758x41 のウィンドウも無かった。xexpose は khome が SHEET を閉じるときだけ 60ms 動く一回限りのプログラムで、この時間には存在しない。
- 直接の引き金は、**KOReader を強制終了した後に `stop/start lab126_gui` だけでは X 側（awesome / `com.lab126.winmgr`）が復旧しなかったこと**と判断する。ダンプ中のログには `com.lab126.winmgr` の ASRMode / orientation の lipc タイムアウトと、contentpackd の「User store available signal not received」が繰り返し出ており、cvm はアプリのウィンドウを map できないまま固まっていた。`stop/start lab126_gui` は `x` ジョブを再起動しないため、winmgr が固まったまま cvm だけが上がり、約 2 分後に dumpstack の対象になった、という筋。
- `x` ジョブも再起動すると、同じ手順（`stop lab126_gui; stop x; start x; start lab126_gui`）で cvm は正常にホームまで上がり、以後コアは増えていない（16544、および最後の 22421）。
- ただし、ダンプ時のログが tinyrot で消えており、クラッシュレポートにも理由行が無いため、**KOReader の強制終了が唯一の原因だとは断定できない**。事実として言えるのは「khome / xband / xexpose が動いていない時間に、GUI 再起動後の cvm が 2 回 dumpstack された」ことと「X ジョブの再起動で直った」ことまで。

### 8.3 fd 9 の継承と khome.sh の再起動

`kindle/khome/khome.sh` の fakekaf 起動を `"$FAKEKAF" >>... 2>&1 9>&- &` にした。実機で fakekaf の fd を確認:

```text
fakekaf=6624
0 -> /dev/null
1 -> /mnt/us/khome/fakekaf.log
2 -> /mnt/us/khome/fakekaf.log
（fd 9 は無い）
```

さらに、fakekaf 以外も fd 9 を継承していた。

- `launcher/src/lipc.c` の `lipc_watch_start()` の子（`lipc-wait-event`）が fd 9 を持ち続けた。khome を SIGKILL した後に `khome.sh` を起動すると `another launcher holds` で失敗し、`/proc/*/fd` を見ると `lipc-wait-event` 2 本が `/mnt/us/khome/khome.pid` を掴んでいた（pid 6651, 6654）。子で `close(9)` するようにした。
- khome が起動する KOReader とそのプラグインサーバも fd 9 を継承した。KOReader を強制終了した後、`/mnt/us/koreader/plugins/rakuyomi.koplugin/server ./rakuyomi` が fd 9 を掴んだまま残り、再び `another launcher holds` になった。
- そこで khome 本体の起動時に fd 9 へ `FD_CLOEXEC` を立てる `lock_fd_close_on_exec()` を足した（`launcher/src/main.c`）。以後 khome が exec する子（watcher、xexpose、KOReader、プラグイン）は fd 9 を継承しない。khome 自身は wrapper の flock を保持したまま。

再起動の実機確認（fakekaf を生かしたまま khome を落とす）:

1. 修正前: wrapper と khome を `kill -9` → fakekaf は生存 → 新しい `khome.sh` は `another launcher holds /mnt/us/khome/khome.pid` で終了。fd 9 の保持者は `lipc-wait-event` だった。
2. 修正後: wrapper と khome を `kill -9` → fakekaf 9120 は生存、`khome.pid` を掴むプロセスは 0 → `khome.sh` がそのまま起動し、`khome: start` と `EVIOCGRAB 1` まで進んだ（16:39:48）。
3. 最後の khome 停止後も、`/proc/*/fd` で `khome.pid` を持つのは wrapper と khome だけだった。

### 8.4 sheet_close（表示中タブ / 最後の 1 枚）

2 タブ（active=0: example.com、1: google.co.jp）で実機確認。

- 表示中タブの close: シートの 1 行目 ×（device (690,216)）をタップ。

```text
16:36:22 tap action sheet_close
16:36:22 SHEET + sheet-close-active -> BROWSER
16:36:22   action sheet-close-active
16:36:22 browser: opening https://www.google.co.jp/
16:36:22   action ungrab
16:36:22   action hide-sheet
16:36:23 sheet: xexpose exposed
```

`tabs.txt` は `active=0` + google 1 行になり、`95-close-active.png` は Google のページ。判定 **OK**。

- 最後の 1 枚の close: 同じ位置をタップ。

```text
16:36:42 tap action sheet_close
16:36:42 SHEET + sheet-close-last -> HOME
16:36:42   action sheet-close-last
16:36:42 sheet: closed the last tab, count 0
16:36:42 browser: kill 1
16:36:42 browser: exited after about 100 ms
16:36:43 browser: alert shown, tapping close
16:36:45 band: stopped
```

`mesquite` は消え、`tabs.txt` は `active=-1` だけ、`96-closed-last.png` は khome のホーム。判定 **OK**。

### 8.5 スワイプ注入の追試

`scripts/dev/ksweep.sh`（`swipegen.py` で作った 1 本のスワイプを、1 回の ssh と 1 回の `cat` で event1 へ書く）を追加した。スロット 0 を明示し、press から release までを 1 つのバイト列にする。

- 通常のシート引き（(379,10) → (379,310)）: **4/4 回** `BROWSER + sheet-pull -> SHEET`。
- 直前のタップがスワイプ開始点と同じ (379,10) のケース（MT スロットに同じ X/Y が残る）: **3/3 回** 成功。X と Y が input core の重複除去で落ちても、khome の `last_x/last_y` が直前タッチの値（= 開始点）を持っているため開始点は正しく、その後の移動で判定できる。
- press / moves / release を別々の ssh で送る分割注入（間隔 0.3 秒と 1.0 秒）: **2/2 回** 成功。
- `touch release without tap` は、シート引きにならない完了済みジェスチャでも出る。帯の外から始めた上スワイプ (379,600) → (379,300) を注入すると、`touch press` の後に `touch release without tap` が出た。つまりこの 1 行だけでは「スワイプを認識し損ねた」とは言えない。
- 同じ座標のタップを続けて注入すると (0,0) になることを再確認した（16:58:35、`tap 0,0 -> design 0,0`）。input core が同じ ABS 値を落とすためで、`ktap.sh` を連続で使うときは座標をずらす必要がある。

結論: **コード側でイベントを取りこぼす経路は見つからなかった**。位相 2 の「7 回中 2 回」は、注入の分け方（複数回の write / ssh）か、`start_y > 40`・移動量不足などジェスチャ条件を満たさない入力だった可能性が高い。区別できるように `touch release without tap` のログへ開始点と終了点を足した:

```text
touch release without tap (start 379,600 end 379,300)
```

### 8.6 変更点（今回の追試）

- `kindle/xband/xband.c` / `build.sh` — 上端 41px の InputOnly override-redirect ウィンドウ。100ms ごとに raise、親の死で終了。
- `launcher/src/main.c` — `sync_band()`（BROWSER/SHEET の出入りで xband を起動/停止）、終了時の停止、`band: exited` の reap、`touch release without tap` に座標、fd 9 の `FD_CLOEXEC`。
- `launcher/src/lipc.c` — `lipc-wait-event` の子で fd 9 を閉じる。
- `launcher/tools/deploy.sh` — xband をビルドして `/mnt/us/khome/bin/xband` へ送る。
- `kindle/khome/khome.sh` — fakekaf 起動時に `9>&-`。
- `scripts/dev/ksweep.sh` / `swipegen.py` — スワイプ注入。
- `launcher/tests/` は触っていない。`./build.sh test` は 75 unit + 19 render が通る。`./build.sh arm` も通る。

### 8.7 残るリスク

1. **帯より下のスワイプでブラウザがスクロールすることは画素で確認できていない。** 16:34 のランでは example.com が短く、帯の外から始めたスワイプでもページが動かなかった（帯のウィンドウが 41px で、それより下に何も置いていないことは `xwininfo` で確認）。その後 17:02 に再確認しようとしたが、**X ジョブを再起動した後の cvm 無し状態では mesquite が起動直後に exitcode=1 で落ち**、ブラウザ自体が開かなかった。pillowd / webreader を手で起動し直しても同じだった。したがって帯の画素確認は 16:34 の 90/91/92 が最後の実測で、長いページでの下側スクロールは未確認。
2. **cvm の dumpstack 2 件の原因は断定できていない**（8.2）。khome / xband / xexpose が動いていない時間に起きており、X ジョブの再起動で復旧した。KOReader を強制終了した状態で `lab126_gui` だけを再起動すると再発する可能性がある。純正復帰は `stop lab126_gui; stop x; start x; start lab126_gui` が確実。
3. **X ジョブを再起動した後、cvm 無しモードのブラウザが起動しないことがある**（上記 1）。stock の cvm では問題なく、khome の cvm 無し経路だけ。mesquite の stderr は残らず、appmgrd には `started_app pid=...` の直後に `exited_app ... exitcode=1, abnormal_exit=1` だけが出る。X 再起動前の 16:34 には同じバイナリ・同じ tabs.txt で動いていたので、X 再起動後の環境（winmgr は `winmgrReady=1` を返す）に依存する状態差とみられる。原因は未特定。
4. **KOReader 起動のきっかけになったタップはこちらではない。** 16:41:11 の `tap action app_manga` は、こちらの注入（452,520 とシート操作）とは別の座標・時刻で、実機のログに残った。誰かが実機の画面を触ったか、別のクライアントが注入した可能性がある。以後のログにはその操作が混ざっている。
5. 帯は BROWSER / SHEET の間ずっと出ている。SHEET 中は khome が event1 を grab しているので害はないが、X のウィンドウは 1 枚増えたままになる。
6. xband は 100ms ごとに `XRaiseWindow` する。CPU 負荷は無視できるが、X のリクエストは増える。
7. **同じ座標のタップを続けて注入すると (0,0) になる**（8.5）。`ktap.sh` を連続で使うときは座標をずらす。スワイプは Y が動くので影響しない。

### 8.8 最終状態

最後の khome.sh に SIGTERM を送った後、純正へ戻した（17:03:59 に送信、17:06 にホーム）。

```text
flag-absent
cvm=25527  fakekaf=[]  khome=[]  xband=[]
activeApp=com.lab126.booklet.home
blanket load = screensaver langpicker blankwindow usb   (splash 無し)
framework start/running
lab126_gui start/running
```

`99-stock-home-final.png` は My Kindle / マイライブラリ。`/mnt/us/cvm_*.core` は 8.2 の 2 個のままで、増えていない。

