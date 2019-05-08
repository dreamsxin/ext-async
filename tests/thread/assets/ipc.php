<?php

namespace Concurrent;

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

var_dump(Thread::isWorker());

$ipc = Thread::connect();
$ipc = Thread::connect();

try {
    var_dump($ipc->read());
    $ipc->write('World');
    
    usleep(250000);
    
    var_dump('EXIT');
} finally {
    $ipc->close();
}
