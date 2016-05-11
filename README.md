- TODO:
    - logging options
    - error handing
    - README
    - systemd recipe
    - security
    - avoid socket inf loop when connect localhost or cyclic ssh
        - random choose slave socket OR allow only one connection per slave socket at once?

# かんたんなつかいかた (with ssh-agent)

1. socket_multiplexer をバックグラウンドで動かす

    めんどい場合はscreenの中とかでok (TODO: そのうちsystemd向けのserviceファイルこしらえる)

    ```bash
    socket_multiplexer -c /tmp/socket_multiplexer_control -s /tmp/socket_multiplexer
    ```

1. .bashrcでSSH_AUTH_SOCKをフックするようにする

    たとえばこんな感じ。

    ```bash
    # ssh-agent使ったセッション、かつ、socket_multiplexerが動いてるなら
    if [ -n "${SSH_AUTH_SOCK}" \
      -a -S /tmp/socket_multiplexer_control \
      -a -S /tmp/socket_multiplexer ]; then

      # このセッションのSSH_AUTH_SOCKをsocket_multiplexerに追加
      nc -U /tmp/socket_multiplexer_control <<<"ADD ${SSH_AUTH_SOCK}" >/dev/null

      # 念のためSSH_AUTH_SOCKのバックアップをとって
      ORIGINAL_SSH_AUTH_SOCK="${SSH_AUTH_SOCK}"

      # socket_multiplexerに繋ぎ変えてしまう。
      SSH_AUTH_SOCK=/tmp/socket_multiplexer
    fi
    ```

1. 普通にsshで繋いだり、screenをデタッチ/アタッチしたりする。

    sshがagentを要求する際、socket_multiplexerを通じて生きているソケットに勝手に繋がるようになるので、
    screenのデタッチ/アタッチなどで環境変数を気にする必要がない！ しあわせ！

1. そのほか、nc(1)などでソケットにメッセージを送ることで、操作が多少できます。

    ```console
    $ # socket_multiplexerにソケットを追加する。
    $ nc -U /tmp/socket_multiplexer_control <<<"ADD /tmp/ssh-YXWG8gnlm1/agent.6715"
    ADDed /tmp/ssh-YXWG8gnlm1/agent.6715

    $ # ソケット一覧を確かめる
    $ nc -U /tmp/socket_multiplexer_control <<<"LIST"
    /tmp/ssh-rqzUdGXohX/agent.2751
    /tmp/ssh-NCBXOhOmAX/agent.14009
    /tmp/ssh-FXjiQezLPa/agent.3413
    /tmp/ssh-HgfZlyTnCv/agent.4955
    /tmp/ssh-YXWG8gnlm1/agent.6715

    $ # ソケットをメンバから取り除く
    $ nc -U /tmp/socket_multiplexer_control <<<"DELETE /tmp/ssh-NCBXOhOmAX/agent.14009"
    DELETEed /tmp/ssh-NCBXOhOmAX/agent.14009

    $ # 全部登録だ！
    $ \ls -1 /tmp/ssh-*/agent.* | sed 's/^/ADD /' | nc -U /tmp/socket_multiplexer_control

    $ # 終了する
    $ nc -U /tmp/socket_multiplexer_control <<<"QUIT"
    ```
