# 0001. ブラウザの向きをホームのトグルと settings.orientation で切り替える

## Status

Accepted (2026-10-25)

## Context

khome のホームから開く標準ブラウザは mesquite（WAF + WebKitGTK）で、窓は
`ID:com.lab126.browser`。回転は awesome（`com.lab126.winmgr`）がウィンドウ名の
`_O:` で許可した窓だけに起きる。`_O:` の無いブラウザは U/D のままで、縦の
758x1024 で開く。winmgr の orientation はこのファームウェアでは読み取り専用で、
`orientationLock` を書いても `_O:` の無いブラウザは回らない（横向きでも
`orientation` = `L`、`orientationLock` = `U`）。加速度センサーは無いので、
傾けても回らない。

向きの設定はブラウザ自身の localStorage にある。実体は
`/mnt/us/.active_content_sandbox/browser/resource/LocalStorage/file__0.localstorage`
の `ItemTable` で、`key='settings'` の行が向きを保持する。この値は
browser.js → device.js → `kindle.device.setOrientation` と呼び出され、
ネイティブ側が窓に `_O:` を付ける。`landscapeLeft` で `_O:L`・1024x758・
`orientation` = `L`・fb0 `rotate` = 0、`auto` で `_O:` なし・758x1024・
`orientation` = `U`・`rotate` = 3。

khome のキャンバスは常に縦の 758x1024（stride 768）で、横向きの間は
フレームバッファが 1024x758（stride 1024）になる。横向きのままシートを
描くと縦の stride の画素が横ずれの縞になり、`shield: started ... 758x291`
が左上を覆って操作できなくなった（2026-10-24 の試験）。専用の戻り経路が要る。

## Decision

ホームの Browser タイル直下のトグル（`browser_orientation`）を押すと、
`main.c` の `browser_orientation_toggle()` が `settings.orientation` を
`landscapeLeft` か `auto` に書き換える。手順は次の順に固定する。

1. ブラウザが動いていれば `browser_stop()` で mesquite を止める（WebKit が
   localStorage をメモリに持つため）。その後 `wait_for_portrait()` を呼ぶ。
2. `select json_valid(value), hex(value) from ItemTable where key='settings';`
   を `-readonly` で読み、`hex()` を C 側で復号して現行値を控える。
   `json_valid(value)` が 0 なら何も書かずに終わる。
3. 値だけを差し替える固定 SQL を流す。
   `update ItemTable set value=json_set(value,'$.orientation','landscapeLeft')
   where key='settings' and json_valid(value);`（縦は `'auto'`）。
4. 書き込みに失敗した場合と、読み戻して `tabs_browser_orientation()` が意図
   した値でない場合は、手順 2 の hex を `update ItemTable set value=CAST(X'...'
   AS TEXT) where key='settings';` で書き戻す。

壊れた JSON は縦として扱う。`tabs_browser_orientation()` は文書全体を構文
解析し、切れたオブジェクトや余分な文字があれば
`BROWSER_ORIENTATION_PORTRAIT` を返す。

横向きの間は次のように変える。
- シートは出さない。`screen_landscape()`（`model.browser_landscape` かつ
  BROWSER/SHEET/EDIT）では上端プルを `KHOME_EV_BROWSER_CLOSE` に流し、直接ホームへ戻す。
- 上端プルは `gesture_is_top_pull()` が判定する。パネルは横向きでも縦の
  座標（758x1024）を報告するので、縦の上端 `y <= 96` に加えて両長辺
  `x <= 96` / `x >= 757-96` からの内側プル（120 px 以上、横のぶれは移動量
  の半分以下）も受ける。どちらの長辺が上かは測っていないため両方。
- バンドも出さない（`band_state()` が `model.browser_landscape` で 0 を
  返す）。縦座標の X ウィンドウで、横向きの上端に合わない。
- 閉じたあとは `wait_for_portrait()` が `/sys/class/graphics/fb0` の寸法を
  読み直し、758x1024 に戻るまで `KHOME_PORTRAIT_WAIT_MS`（1000 ms）待つ。
  `draw_frame()` も `fb_matches_canvas()` が偽なら描かない。`browser_stop()` /
  `browser_exited()` / `browser_launch_abort()` の戻り経路もここを通る。

却下した案:
- `lipc-set-prop com.lab126.winmgr orientationLock` で回す。利用者が承認して
  いない端末全体の設定書き換えであり、`_O:` の無い窓はこれでは回らない。
- `/sys/class/graphics/fb0/rotate` を直接書く。記録にあるのは回転に伴う
  3（縦）と 0（横）の入れ替わりという観測だけで、直接書く試験はしていない。
- CSS transform でページだけ回す。試験はしていない。

## Consequences

良い点:
- 回転はネイティブ（winmgr とフレームバッファ）が行う。ページは 1024x758 で
  組まれ、文字も画像も拡大されない。
- 設定はブラウザ自身なので、再起動後も Browser を押せば横で開く（`start_browser()` が起動前に読む）。
- `json_set` は `$.orientation` だけを変え、bookmarks / lastUrl と他の設定キーも hex 一致。

制約:
- 横向きの間はシートが無く、戻る手段は上端プルだけ。タブ切り替えやシートの Home は使えない。
- タッチ座標はパネルの縦座標のままで、端の判定は両長辺を特別扱いしている。
- トグルは設計 208x40、実機で x 379–526 / y 600–628（約 147x28）と小さい。
- どちらの長辺が上かは未確定で、両方を受けるため横向きでは左右どちらの端から引いてもホームに戻る。
- 横向きではバンドが無く、ブラウザ表示中に時計・電池は見えない。
- トグルは history を触らない（変わるのはブラウザ起動時の書き込みだけ）。

ロールバック: 旧バイナリは `/mnt/us/khome/.pre-rotation`（sha256
`a613bde3...`）。起動中は `cp` が ETXTBSY なので `khome.new` に写し、
`chmod 755`、`mv -f` で差し替え、wrapper（`/bin/sh khome.sh`）に `kill -TERM`
してから `nohup /mnt/us/khome/khome.sh >/dev/null 2>&1 &` で起動し直す
（`.audit/rotation-toggle/README.md` の「戻し方」）。設定だけならトグルを
Portrait にすれば `auto` が書かれ、退避値と hex 一致になる。DB ごと戻すなら
`/mnt/us/khome/rotation-toggle-backup/file__0.localstorage.orig` を
`/mnt/us/.active_content_sandbox/browser/resource/LocalStorage/file__0.localstorage`
へコピーする（`rotation-retest-backup/` にも控えがある）。

守るべき端末の制約: `/var/local` に書かず、`mntroot rw`、`reboot`、`fw_setenv`、
`dd` も使わない。書き込みは `/mnt/us` と、入力注入が使う実機 `/tmp` の範囲だけ。

## 参照

- `.audit/rotation-test/README.md` — 直接書き換えた最初の試験（復元まで）。
- `.audit/rotation-toggle/README.md` — トグルの実機確認、横向きシートの不具合、戻し方。
- `.audit/rotation-toggle/evidence/retest.txt` — 横向きの上端プルで直接ホームへ戻る再試験。
- commit `c64cea0` — `browser_settings_*`、`browser_orientation_toggle`、
  `wait_for_portrait`（`launcher/src/main.c`）、`gesture_is_top_pull`
  （`launcher/src/gesture.c`）、`tabs_browser_orientation`（`tabs.c`）。
