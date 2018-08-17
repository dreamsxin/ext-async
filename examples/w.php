<?php

namespace Concurrent\Network;

$socket = TcpSocket::connect('localhost', 8080);

try {
    $socket->write('Hello World :)');
    $socket->writeStream()->close();
    
    while (null !== ($chunk = $socket->read())) {
        var_dump($chunk);
    }
} finally {
    $socket->close();
}
