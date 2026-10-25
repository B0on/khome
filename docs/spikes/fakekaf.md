# 代役 kaf（fakekaf）: cvm 無しで powerd の電源ボタンを有効にする

2026-10-23。Paperwhite 2（758×1024）/ FW 5.12.2.1.1 / `ssh kindle`（<kindle-ip>, root）。
コマンドはすべて先頭に `export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin DISPLAY=:0;` を付けて実行した。
この作業では **fakekaf を端末で常駐起動していない**。`--help` / `--version` / `--selftest`（LIPC にサービス登録しない読み取りのみ）だけを実行した。

## 結論（要約）

- powerd が電源ボタンを無視するかどうかは、`powerd` の **0x16634** の関数 1 箇所で決まる。ここでは次の 2 つを順に確認している。
  1. `LipcGetIntProperty(com.lab126.kaf, "frameworkStarted")` が成功し、値が **非 0** であること（0x166d8〜0x166f0）。
  2. `LipcGetStringProperty(com.lab126.blanket, "load")` が成功し、その文字列に **`splash` が含まれない**こと（0x16780〜0x167c0）。
- どちらかが失敗すると 0x1c7ac の呼び出し元が 0 を受け取り、`Splash screen is on.  Ignoring power button`（文字列 0x2b3ad）を出して電源ボタンを捨てる。cvm が無いときのログはこれ。
- 実機の本物の kaf は `frameworkStarted` を **Int / 読み取り専用**で公開し、cvm 起動完了時に 1 を返す。`FrameworkStarted` イベントは **引数なし**で送られる（実測）。
- powerd は kaf のイベントを 3 つ購読している（`tzRequested` / `twilightCacheCreated` / `FrameworkStarted`）が、いずれも **cafl（フロントライトの自動スケジューラ）用**で、電源ボタンの判定には使われていない。
- スクリーンセーバー描画を kaf に依頼する箇所は無い。powerd は `goingToScreenSaver` イベントを出し、自分の `g_is_screensaver_drawn` を 1 にして、`device_can_suspend` なら `prv_start_t2_timer`（0x19060）で t2 タイマーを開始する（0x1ac10〜0x1acf8）。実画面を描くのは `com.lab126.screensaver` / blanket 側。
- `kindle/fakekaf/fakekaf.c` は `com.lab126.kaf` を名乗り、`frameworkStarted=1` を返す。必要に応じて blanket の `splash` を外し、`FrameworkStarted` を送る。`kindle/fakekaf/build.sh` で再ビルドできる。
- 端末 `/mnt/us/khome-spike/fakekaf` に配置し、`file` / `ldd` / `--selftest` で依存解決と LIPC 接続まで確認した。**常駐起動はしていない**。

## 1. powerd の逆アセンブル

### 1.1 バイナリの取得

```sh
ssh kindle 'which powerd'          # /usr/bin/powerd
ssh kindle 'cat /usr/bin/powerd' > powerd
file powerd
```

```text
powerd: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
  dynamically linked, interpreter /lib/ld-linux.so.3, for GNU/Linux 2.6.32, stripped
```

`objdump -d powerd`（Apple LLVM objdump）で読んだ。`powerd` は stripped なので関数名は無い。以下のアドレスは `objdump` の表示そのまま（`.text` の仮想アドレス）。

### 1.2 「スプラッシュ中か」を返す関数: 0x16634

処理を疑似コードにすると次のとおり。

```c
/* powerd 0x16634。戻り値 1 = 電源ボタンを処理してよい、0 = スプラッシュ中 */
int framework_ready_and_no_splash(void)
{
    /* 0x16638: r1 = "system/daemon/powerd/KAF_NAME" (0x28b10) */
    name = dynconf("system/daemon/powerd/KAF_NAME");            /* → "com.lab126.kaf" */

    /* 0x16694: r1 = "system/daemon/powerd/KAF_FRAMEWORK_STARTED" (0x28b2e) */
    prop = dynconf("system/daemon/powerd/KAF_FRAMEWORK_STARTED"); /* → "frameworkStarted" */

    /* 0x166d8: LipcGetIntProperty(handle, name, prop, &value) */
    if (LipcGetIntProperty(h, name, prop, &value) != 0)
        return 0;                        /* サービス/プロパティが無い、型違い、タイムアウト */
    if (value == 0)                      /* 0x166f0 */
        return 0;

    /* 0x166f8: r1 = ".../BLANKET_NAME" (0x28b59) */
    bname = dynconf("system/daemon/powerd/BLANKET_NAME");      /* → "com.lab126.blanket" */
    /* 0x16738: r1 = ".../BLANKET_LOAD" (0x28b7b) */
    bprop = dynconf("system/daemon/powerd/BLANKET_LOAD");      /* → "load" */

    /* 0x16780: LipcGetStringProperty(handle, bname, bprop, &load) */
    if (LipcGetStringProperty(h, bname, bprop, &load) != 0)
        return 0;                        /* blanket が無い等 */

    found = strstr(load, "splash") != NULL;  /* 0x167a0 */
    LipcFreeString(load);

    /* 0x167a4〜0x167c0: strstr が見つかったとき r0 = 0、
       見つからなかったとき r0 = 1 を返す */
    return !found;
}
```

根拠となる主な命令:

| アドレス | 内容 |
| --- | --- |
| 0x16638 | `ldr r1, [pc, #0x18c]` → 0x167cc のリテラル = 0x28b10 = `system/daemon/powerd/KAF_NAME` |
| 0x1665c〜0x1668c | `ksLookupByName` + `keyValue` で kaf のサービス名を取得 |
| 0x16694 | `ldr r1, [pc, #0x140]` → 0x167dc = 0x28b2e = `.../KAF_FRAMEWORK_STARTED` |
| 0x166d8 | `bl LipcGetIntProperty@plt` |
| 0x166dc〜0x166e4 | 戻り値が非 0 なら `return 0` |
| 0x166f0〜0x166f4 | 値が 0 なら `return 0` |
| 0x166f8〜0x16704 | blanket のサービス名を dynconf から取得 |
| 0x16738〜0x16744 | blanket のプロパティ名を dynconf から取得 |
| 0x16780 | `bl LipcGetStringProperty@plt` |
| 0x167a0 | `bl strstr@plt`（第 2 引数は 0x28b9d = `splash`） |
| 0x167b0〜0x167c0 | `splash` が見つかったとき r0=0、見つからないとき r0=1 |

### 1.3 呼び出し元（pbisplash）: 0x1c7ac

```text
1c7ac: bl   0x16634
1c7b0: subs r4, r0, #0
1c7b4: bne  0x1c7d8          ; 非 0 → 通常の電源ボタン処理
...
1c7d0: ldr  r2, [pc, #0x6a4] @ 0x1ce7c → 0x2b3ad
1c7d4: b    0x1c83c          ; __syslog_chk でログ
```

- 0x2b3ad の文字列: `I def:pbisplash::Splash screen is on.  Ignoring power button`
- つまり **0x16634 が 0 を返したとき**に「スプラッシュ中」として電源ボタンを捨てる。
- cvm が無いときは `LipcGetIntProperty` が `ServiceUnknown` で失敗するため 0 が返る。ログの文言は「スプラッシュが表示中」だが、実際は **kaf が無いだけ**でもこのメッセージになる。

### 1.4 dynconf のキーと実機での値

`powerd` の文字列と、実機で読んだ LIPC プロパティから次のように確定した。

| キー | 値 | 根拠 |
| --- | --- | --- |
| `system/daemon/powerd/KAF_NAME` | `com.lab126.kaf` | cvm 無しのログ `The name com.lab126.kaf was not provided by any .service files`、および 0x2b10 の文字列 |
| `system/daemon/powerd/KAF_FRAMEWORK_STARTED` | `frameworkStarted` | 実機の `lipc-probe -v com.lab126.kaf` が `r Int frameworkStarted [1]` を返す。`framework_setup.conf` も同じ名前を使う |
| `system/daemon/powerd/BLANKET_NAME` | `com.lab126.blanket` | 実機の `lipc-probe -v com.lab126.blanket`、および 0x28b59 の文字列 |
| `system/daemon/powerd/BLANKET_LOAD` | `load` | 同上（`rw Str load [ ... ]`） |
| `system/daemon/powerd/BOOT_SPLASH_CLEANUP_EVENT` | `bootSplashCleanup`（推定） | 0x28953 のキー名と `/etc/upstart/framework_setup.conf` の `lipc-send-event ... com.lab126.hal bootSplashCleanup`。電源ボタン判定では使われない |

注: dynconf の実体ファイルは `/etc` / `/var/local/system` / `/usr/share` を探したが見つからなかった。値は「実機の LIPC プロパティ」と「powerd の文字列」からの確定・推定である。

### 1.5 powerd が kaf に依存している他の箇所

`strings powerd | grep -i kaf` で出るのは `KAF_NAME` と `KAF_FRAMEWORK_STARTED` の 2 つだけ。逆アセンブルすると、powerd は起動時に kaf のイベントを 3 つ購読している（すべて cafl / フロントライト用）。

| アドレス | source | event | handler | 用途 |
| --- | --- | --- | --- | --- |
| 0x16ea4〜0x16ef0 | `KAF_NAME`（com.lab126.kaf） | `tzRequested`（0x28d83） | 0x14020 | タイムゾーン変更で cafl スケジューラを再起動 |
| 0x16f78〜0x16fa0 | 同上 | `twilightCacheCreated`（0x28d8f） | 0x14074 | 薄明情報の更新で cafl を再起動 |
| 0x17028〜0x17050 | 同上 | `FrameworkStarted`（0x28da4） | 0x14074 | framework 起動後に cafl を再起動 |

- 0x17048 が `FrameworkStarted` の文字列（0x28da4）をロードし、0x17050 で `LipcSubscribeExt` を呼ぶ。
- 0x14020 / 0x14074 はどちらも `device_has_cafl` と cafl の状態を見て、必要なら 0x23c78（cafl 再起動）に進む。電源ボタンの判定には関係しない。
- **kaf にスクリーンセーバー描画を依頼する箇所は無い。** powerd の screenSaver 入口 0x1ac10 は、
  - 0x1ac68: `goingToScreenSaver`（0x2aa31）を送る
  - 0x1aca0: `g_is_screensaver_drawn = 1` にする
  - 0x1accc: `device_can_suspend` を確認
  - 0x1acd8: 0x19060（`prv_start_t2_timer`、suspend 移行用の t2 タイマー開始）へ進む
  という流れで、Java の `com.lab126.screensaver` が無くても状態遷移とタイマー開始は進む。実際の画面を描くのは `com.lab126.screensaver` / blanket 側で、kaf ではない。

### 1.6 「10 秒タイムアウト」について

- powerd は `LipcGetIntProperty` を明示的なタイムアウト無しで呼ぶ（0x166d8）。
- 端末の `liblipc.so.1.0` のプロパティアクセスのデフォルトは **10000 ms**。`openlipc.h:394-402` に「`/var/local/system/lipctimeout` が無ければ 10000 ms」とある。実機に `/var/local/system/lipctimeout` は無い。
- ただしサービス自体が存在しない場合、今回のログでは `ServiceUnknown` が即座に返った（10 秒待っていない）。10 秒待つのは「サービスはあるが応答しない/遅い」場合。

## 2. 実機で確認したプロパティとイベント

### 2.1 cvm 停止中

```sh
lipc-get-prop com.lab126.kaf frameworkStarted
# com.lab126.kaf failed to access property frameworkStarted (0x3 lipcErrNoSuchSource)  rc=3
lipc-get-prop com.lab126.blanket load
# screensaver langpicker blankwindow usb
lipc-get-prop com.lab126.powerd state
# active
```

kaf が無い。blanket の `load` に `splash` は無い（GUI 停止後は splash が外れている）。

### 2.2 cvm 起動中（framework 起動の途中）

```sh
lipc-get-prop com.lab126.kaf frameworkStarted   # 0
lipc-get-prop com.lab126.blanket load
# screensaver langpicker blankwindow usb splash
```

この時点では blanket に `splash` が残っている。この状態で電源ボタンを押すと、たとえ kaf が居ても 0x16634 は 0 を返す（`splash` が見つかるため。逆アセンブルからの推論）。

### 2.3 cvm 起動完了後

```sh
lipc-probe -v com.lab126.kaf | grep -E 'frameworkStarted|delayFrameworkStart'
#   rw  Int  delayFrameworkStart  [0]
#   r   Int  frameworkStarted     [1]
lipc-get-prop com.lab126.kaf frameworkStarted   # 1
lipc-get-prop com.lab126.blanket load
# screensaver langpicker blankwindow usb
```

`frameworkStarted` は **読み取り専用の Int**。`FrameworkStarted` という大文字始まりのプロパティは存在しない（`NoSuchProperty`）。プロパティは小文字、イベントは大文字である点に注意。

### 2.4 `FrameworkStarted` イベントの実測

cvm が起動するのを待って、読み取り専用のリスナーで捕捉した。

```sh
lipc-wait-event -m -t -s 150 com.lab126.kaf FrameworkStarted
```

出力:

```text
[06:25:19.613512] FrameworkStarted
```

引数は無い。イベント名は `FrameworkStarted`（大文字始まり）で、プロパティ名 `frameworkStarted` とは別物。

## 3. fakekaf の設計

実装: `kindle/fakekaf/fakekaf.c`、ヘッダ: `kindle/fakekaf/openlipc.h`（openlipc, MIT、`LICENSE.openlipc` を同梱）。

### 3.1 動作

1. `--help` / `--version` は LIPC に触らず即終了する。
2. `SIGTERM` / `SIGINT` / `SIGHUP` で `g_stop` を立て、1 秒ループから抜けて `LipcClose` する。
3. `LipcOpenEx("com.lab126.kaf")` でサービスを登録する。cvm が動いていて本物の kaf が登録済みなら、名前が衝突する（実機での正確な挙動は未確認）。cvm が動いている状態では起動しないこと。`LipcOpenEx` が失敗した場合は **exit 1** する。
4. `LipcRegisterIntProperty("frameworkStarted", getter, NULL, NULL)` で **読み取り専用 Int** を登録する。getter は `g_started`（既定 1）を返す。
5. `--keep-splash` が無ければ、`com.lab126.blanket` の `load` を読み、`splash` トークンがあれば `LipcSetStringProperty(com.lab126.blanket, "unload", "splash")` を呼ぶ。`DONT_START_FRAMEWORK` のように framework_setup が動かない構成では、この splash が残ると 0x16634 が 0 のままになるため。
6. `--no-event` が無ければ `LipcCreateAndSendEvent("FrameworkStarted")` を送る。本物の kaf と同じ引数なしイベント。ただし電源ボタンの判定には不要（3.3 参照）。
7. `sleep(1)` ループで常駐する。

### 3.2 なぜ stdio を使っていないか

ビルド直後の版は `fprintf(stderr, ...)` を使っており、端末で `--version` すら **SIGSEGV** した。gdb では `ld.so` の `_dl_relocate_object` 内、`str r12, [r4]`（`pc=0x4000b8bc`、`r4=0x10070`）で落ちていた。lld が `stderr` / `stdout` を **GLOB_DAT** でリンクし、端末の glibc 2.20 の ld.so がそのリロケーションを処理しきれないためである（`write(2)` に変えたら解消）。

そのため `fakekaf.c` は `write(2)` と `vsnprintf` / `snprintf` だけを使い、`stdout` / `stderr` のデータシンボルを動的シンボルテーブルに持たない。確認:

```sh
objdump -T build/fakekaf | grep -E 'stdout|stderr'   # 何も出ない
```

### 3.3 `FrameworkStarted` を送る意味

- 電源ボタンの gate は **プロパティ読み取りだけ**で、イベントは不要。
- 本物の kaf はイベントを送るので、互換性のために既定で送る。`--no-event` で止められる。
- powerd の `FrameworkStarted` ハンドラ（0x14074）は cafl スケジューラの再起動を行う。fakekaf には `tzRequested` / `twilightCacheCreated` が無いので、cafl の薄明更新は期待できない。電源ボタンには影響しない。

### 3.4 制限

- 本物の kaf のプロパティ/イベントをすべて実装したものではない。`frameworkStarted` 以外は返さない。
- `com.lab126.screensaver` の代わりにはならない。画面は白/直前のままになる可能性がある。
- cvm を起動する前に必ず止めること。cvm の kaf は同じサービス名を登録するので、fakekaf が居ると cvm 側の登録が失敗する可能性が高い。

## 4. ビルド

### 4.1 ツールチェーン

- `zig 0.17.0` の `zig cc`（`launcher/build.sh` の arm ビルドと同じ系統）。
- ターゲット: `arm-linux-gnueabi.2.20`（armel / soft-float）、`-mcpu=cortex_a9`、`-mfloat-abi=soft`。
- 端末の glibc は 2.20（`/lib/libc.so.6` を実行して確認: `GNU C Library (GNU libc) stable release version 2.20`）。
- 端末には `crt1.o` / `crti.o` / `crtn.o` が置かれていない（`ls /usr/lib/crt*.o` が空）。そのため CRT は zig の `arm-linux-gnueabi.2.20` のものを使い、**liblipc と libc のシンボル解決は sysroot にコピーした端末の実ライブラリ**で行う。

### 4.2 sysroot

`kindle/fakekaf/sysroot/` は `.gitignore` 済み。中身は端末からコピーした次のファイル（実体と symlink）。

```text
lib/ld-2.20.so, lib/ld-linux.so.3
lib/libc-2.20.so, lib/libc.so.6
lib/libpthread-2.20.so, lib/libpthread.so.0
lib/librt-2.20.so, lib/librt.so.1
lib/libm-2.20.so, lib/libm.so.6
lib/libdl-2.20.so, lib/libdl.so.2
lib/libgcc_s.so.1
usr/lib/liblipc.so.1.0, liblipc.so, liblipc.so.1
usr/lib/libdbus-1.so.3.4.0, libdbus-1.so.3
usr/lib/libcjson.so
usr/lib/libglib-2.0.so.0.2918.0, libglib-2.0.so.0
usr/lib/libgthread-2.0.so.0.2918.0, libgthread-2.0.so.0
usr/lib/libenvload.so.1.0, libenvload.so
usr/lib/libc.so
```

再取得する場合（`kindle/fakekaf/sysroot` で実行）:

```sh
ssh kindle 'cd /; tar -cf - \
  lib/ld-2.20.so lib/ld-linux.so.3 \
  lib/libc-2.20.so lib/libc.so.6 \
  lib/libpthread-2.20.so lib/libpthread.so.0 \
  lib/librt-2.20.so lib/librt.so.1 \
  lib/libm-2.20.so lib/libm.so.6 \
  lib/libdl-2.20.so lib/libdl.so.2 \
  lib/libgcc_s.so.1 \
  usr/lib/liblipc.so.1.0 usr/lib/liblipc.so usr/lib/liblipc.so.1 \
  usr/lib/libdbus-1.so.3.4.0 usr/lib/libdbus-1.so.3 \
  usr/lib/libcjson.so \
  usr/lib/libglib-2.0.so.0.2918.0 usr/lib/libglib-2.0.so.0 \
  usr/lib/libgthread-2.0.so.0.2918.0 usr/lib/libgthread-2.0.so.0 \
  usr/lib/libenvload.so.1.0 usr/lib/libenvload.so \
  usr/lib/libc.so' | tar -xf -
```

### 4.3 ビルド

```sh
cd kindle/fakekaf
./build.sh
```

出力:

```text
zig cc (arm-linux-gnueabi.2.20, armel/soft-float) -> build/fakekaf
built build/fakekaf
```

`DT_NEEDED`:

```text
NEEDED     liblipc.so.1
NEEDED     libc.so.6
```

`-Wl,--allow-shlib-undefined` を付けているのは、liblipc の依存（dbus/glib 等）をリンク時にすべて解決しなくてもよいようにするため。実行時は端末側の実ライブラリで解決される。

## 5. 端末への配置と依存解決の確認

```sh
scp kindle/fakekaf/build/fakekaf kindle:/mnt/us/khome-spike/fakekaf
ssh kindle
```

端末での確認（**起動はしていない**。`--selftest` はサービス登録をしない読み取りのみ）:

```text
# file /mnt/us/khome-spike/fakekaf
/mnt/us/khome-spike/fakekaf: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
  dynamically linked, interpreter /lib/ld-linux.so.3, for GNU/Linux 2.0.0,
  with debug_info, not stripped

# ldd /mnt/us/khome-spike/fakekaf
        /usr/lib/libenvload.so (0x401d7000)
        liblipc.so.1 => /usr/lib/liblipc.so.1 (0x4008f000)
        libc.so.6 => /lib/libc.so.6 (0x401e0000)
        libgcc_s.so.1 => /lib/libgcc_s.so.1 (0x40150000)
        libcjson.so => /usr/lib/libcjson.so (0x40045000)
        libdbus-1.so.3 => /usr/lib/libdbus-1.so.3 (0x400f4000)
        libpthread.so.0 => /lib/libpthread.so.0 (0x40057000)
        librt.so.1 => /lib/librt.so.1 (0x400ac000)
        libgthread-2.0.so.0 => /usr/lib/libgthread-2.0.so.0 (0x400d7000)
        libglib-2.0.so.0 => /usr/lib/libglib-2.0.so.0 (0x40313000)
        /lib/ld-linux.so.3 (0x4000f000)
        libm.so.6 => /lib/libm.so.6 (0x4040a000)

# /mnt/us/khome-spike/fakekaf --version
fakekaf 0.1
# /mnt/us/khome-spike/fakekaf --selftest
fakekaf: selftest: com.lab126.blanket.load = [screensaver langpicker blankwindow usb]
fakekaf: selftest: com.lab126.powerd.state = [active]
```

`ldd` はすべて解決した（`not found` が無い）。`--selftest` は `LipcOpenNoName` で接続し、blanket と powerd の読み取りだけを行った。**`com.lab126.kaf` は登録していない。**

## 6. 実機で試すときの手順案（まだ実行していない）

前提: 別作業者が GUI を止めていて、`pidof cvm` が空、`lipc-get-prop com.lab126.kaf frameworkStarted` が失敗する状態。

```sh
ssh kindle
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin DISPLAY=:0

# 1. 前提確認
pidof cvm                                  # 何も出ないこと
lipc-get-prop com.lab126.kaf frameworkStarted
# -> lipcErrNoSuchSource なら OK
lipc-get-prop com.lab126.blanket load
# -> splash が含まれていないこと（含まれていれば fakekaf が外す）

# 2. 依存と LIPC の確認（登録しない）
/mnt/us/khome-spike/fakekaf --selftest

# 3. fakekaf を常駐
nohup /mnt/us/khome-spike/fakekaf >/tmp/fakekaf.log 2>&1 &
sleep 1

# 4. 登録とプロパティの確認
lipc-probe -v com.lab126.kaf
#   r Int frameworkStarted [1] が出ること
lipc-get-prop com.lab126.kaf frameworkStarted   # 1
lipc-get-prop com.lab126.blanket load           # splash が無いこと
cat /tmp/fakekaf.log

# 5. powerd のイベントを監視
lipc-wait-event -m -t -s 45 com.lab126.powerd '*' >/tmp/pb.log 2>&1 &

# 6. 電源ボタン（ソフトウェア）を 1 回
lipc-set-prop -i com.lab126.powerd powerButton 1
sleep 3
cat /tmp/pb.log
# 期待: goingToScreenSaver 2
#       （実画面は com.lab126.screensaver が無いので白/直前のままの可能性）

# 7. もう一度押して復帰
lipc-set-prop -i com.lab126.powerd powerButton 1
sleep 3
cat /tmp/pb.log
# 期待: t1TimerReset, outOfScreenSaver 1, exitingScreenSaver

# 8. fakekaf を止める（cvm を起動する前に必ず）
kill $(pidof fakekaf)
cat /tmp/fakekaf.log

# 9. GUI を戻す
/sbin/start lab126_gui
```

判定の見方:

- 失敗時は `/var/log/messages` に `pbisplash:Splash screen is on.  Ignoring power button` が出る。このとき `lipc-get-prop com.lab126.blanket load` に `splash` が残っていないか、`lipc-get-prop com.lab126.kaf frameworkStarted` が 1 かを確認する。
- `goingToScreenSaver` が来れば、powerd の gate は通っている。suspend まで進むかは `lipc-wait-event` の `com.lab126.powerd` イベントと `/var/log/messages` で確認する。
- 画面が変わらないことは失敗とは限らない。`com.lab126.screensaver`（Java）が無いため、期待できるのは「状態遷移と suspend」まで。
- fakekaf を止めずに `start lab126_gui` すると、cvm の kaf がサービス名重複で失敗する可能性が高い。必ず `kill $(pidof fakekaf)` してから戻す。

## 7. 未確認・リスク

1. **fakekaf の常駐起動は未実施**。登録・プロパティ応答・splash unload・イベント送信は、実機では動かしていない。`--selftest` で liblipc のロードと読み取りだけ確認した。
2. **物理の電源ボタンは未確認**。スパイク時点で event0 の読み手がおらず、押せない。ソフトウェアの `powerButton 1` でのみ確認している。
3. **suspend 完了は未確認**。0x1ac10 の流れから `device_can_suspend` なら進むと読めるが、Java の screensaver が無い状態で実際に suspend し、復帰できるかは試していない。
4. **splash unload の実動作は未確認**。`DONT_START_FRAMEWORK` のように blanket に `splash` が残る構成で、fakekaf の blanket `unload=splash` 呼び出しが効くかは実行していない。効かない場合、0x16634 は 0 のままで電源ボタンは無視される。
5. **kaf の他のイベントは未実装**。`tzRequested` / `twilightCacheCreated` を返さないので、cafl の薄明更新は本物の kaf より劣る。電源ボタンには影響しない。
6. **`FrameworkStarted` の副作用**。powerd の 0x14074 は cafl スケジューラを再起動する。fakekaf がイベントを送ると、この経路が走る。不要なら `--no-event` を付ける。
7. **ビルドの注意**。`stderr` / `stdout` を参照すると lld + glibc 2.20 の GLOB_DAT で起動時に SIGSEGV する。fakekaf は `write(2)` に統一して回避している。将来コードを足すときも stdio のデータシンボルを増やさないこと。
8. **sysroot は git 管理外**。`kindle/fakekaf/sysroot/` が無い checkout では `build.sh` が失敗する。4.2 のコマンドで再取得する。
9. **`--selftest` は読み取りのみ**。`lipc-set-prop` / `stop` / `start` / `kill` / `reboot` / `mntroot rw` はこの作業では実行していない。`/` 以下への書き込みもしていない。

## 8. 参照ファイル

- `kindle/fakekaf/fakekaf.c` — 本体
- `kindle/fakekaf/build.sh` — ビルド
- `kindle/fakekaf/openlipc.h` — openlipc ヘッダ（MIT）
- `kindle/fakekaf/LICENSE.openlipc` — 同ライセンス
- `kindle/fakekaf/sysroot/` — 端末ライブラリ（git 管理外）
- `docs/spikes/no-framework-spike.md` 3.1〜3.2 — 元の観察
- `/etc/upstart/framework_setup.conf` — 本物の framework が splash を外す処理と `FrameworkStarted` 待ち
- `/etc/upstart/blanket_functions` — `com.lab126.blanket` の `load` / `unload` の使い方
