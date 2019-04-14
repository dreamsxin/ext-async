<?php

namespace Concurrent;

use Concurrent\Network\TcpSocket;
use Concurrent\Process\Process;

var_dump(Process::isForked());
var_dump($ipc = Process::forked());

var_dump($tcp = TcpSocket::import($ipc));

try {
    while (null !== ($chunk = $tcp->read())) {
        echo $chunk;
    }
} finally {
    $tcp->close();
}
