# Lapilli C++ パーツ

Node で書いたサービスを C++17 へ移すときに、複数のサービスで共有する小さな部品を置く場所。
TypeScript のパッケージ (`packages/`) とは独立していて、pnpm からは触らない。

移す理由は 2 つ。常駐サービスごとに抱える V8 のメモリを減らすことと、npm の依存ツリーを
取り込まないこと。同じ理由で、C++ 側にも外から取ってくるコードは入れない。

## 決めごと

- **取得・同梱したサードパーティのコードを置かない。** 依存は OS / パッケージマネージャが配る
  システムライブラリだけ (`find_package` で見つける)。`FetchContent` / git サブモジュール /
  vcpkg マニフェスト / amalgamation のコピーを足さない。
- パーツは `cpp/<part>/` に 1 つずつ。公開ヘッダは `include/lapilli/<part>.hpp`、ターゲットは
  `lapilli_<part>` と別名 `lapilli::<part>`。
- 1 ファイル 1 責務。サードパーティのヘッダは公開ヘッダに出さない (pimpl / 前方宣言で隠す)。
- テストは自前の最小の枠 (`tests/test.hpp`) で、外部サービスやネットワークに繋がない。
- 書き方は Portus の C++ 版 (`portus/cpp`) に揃える: タブ、なぜを書く日本語の `//` コメント。
  Portus には依存しない。

## パーツ

| パーツ | ターゲット | 依存 | 内容 |
|---|---|---|---|
| [sqlite](sqlite/README.md) | `lapilli::sqlite` | システムの SQLite3 | prepared statement と bind だけで使う SQLite の薄いラッパー |

## ビルドとテスト

CMake 3.16 以上と C++17 のコンパイラ。Debian / Ubuntu なら `libsqlite3-dev` を入れておく。
ビルドディレクトリはリポジトリの外に置くか、`cpp/build*/` を使う (gitignore 済み)。

```sh
cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

`cpp/` がトップのときはテストを作る (`LAPILLI_BUILD_TESTS` の既定が ON)。サービスから取り込んだ
ときは既定で OFF。

変更したら、通常のビルドに加えてサニタイザ付きでもテストを通す (別のビルドディレクトリで)。

```sh
cmake -S cpp -B cpp/build-asan -G Ninja -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake -S cpp -B cpp/build-tsan -G Ninja -DCMAKE_CXX_FLAGS="-fsanitize=thread"
```

システムの libsqlite3 はサニタイザ無しでビルドされているので、サニタイザが見るのはラッパーと
テストの側だけになる。

### Lorica (任意)

社内のハードニング (`lorica_harden_target`) を当てるときは Lorica の checkout を渡す。
渡したときだけ CMake 3.24 以上が要る。

```sh
cmake -S cpp -B cpp/build -G Ninja -DLAPILLI_LORICA_DIR=/path/to/lorica
```

`lapilli_sqlite` とテストの実行ファイルに `lorica_harden_target` を当てる。`cpp/` をまとめて
取り込んでも、パーツ 1 つ (`cpp/<part>`) だけを取り込んでも同じに効く (判定は各パーツが読む
`cmake/LapilliLorica.cmake`)。取り込む側がすでに Lorica を読んでいたり、パーツを複数取り込んだり
しても、Lorica は二重には読まない。

## サービスからの使い方

パーツをまとめて、またはパーツ 1 つだけを `add_subdirectory` する。

```cmake
# まとめて
add_subdirectory(<lapilli>/cpp ${CMAKE_BINARY_DIR}/lapilli)
# パーツ 1 つ
add_subdirectory(<lapilli>/cpp/sqlite ${CMAKE_BINARY_DIR}/lapilli_sqlite)

target_link_libraries(my_service PRIVATE lapilli::sqlite)
```

`<lapilli>` がサービスのソースツリーの外にあるときは、2 つ目の引数 (ビルドディレクトリ) が要る。
どちらの取り込み方でも、`LAPILLI_LORICA_DIR` を渡せば Lorica が当たる。
