<?php

namespace Concurrent;

use Concurrent\Network\TcpSocket;

for ($i = 0; $i < 2000; $i++) {
    Task::async(function () {
        TcpSocket::connect('localhost', 10011);

        var_dump('CONNECTED');
    });
}
