<?php

namespace Concurrent\Process;

use Concurrent\Network\TcpSocket;

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

var_dump($_SERVER['argv'][1]);
var_dump($_SERVER['argv'][2]);

var_dump(Process::isWorker());
$ipc = Process::connect();

var_dump($ipc->read());
$ipc->write('World');

$socket = TcpSocket::import($ipc);
$socket->write(php_ini_loaded_file() ?: 'NO INI FILE');
$socket->close();
