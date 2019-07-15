<?php

/*
 +----------------------------------------------------------------------+
 | PHP Version 7                                                        |
 +----------------------------------------------------------------------+
 | Copyright (c) Martin Schröder 2019                                   |
 +----------------------------------------------------------------------+
 | This source file is subject to version 3.01 of the PHP license,      |
 | that is bundled with this package in the file LICENSE, and is        |
 | available through the world-wide-web at the following url:           |
 | http://www.php.net/license/3_01.txt                                  |
 | If you did not receive a copy of the PHP license and are unable to   |
 | obtain it through the world-wide-web, please send a note to          |
 | license@php.net so we can mail you a copy immediately.               |
 +----------------------------------------------------------------------+
 | Authors: Martin Schröder <m.schroeder2007@gmail.com>                 |
 +----------------------------------------------------------------------+
 */

namespace Concurrent;

use Concurrent\Network\TcpServer;
use Concurrent\Process\Process;

error_reporting(-1);
ini_set('display_errors', '1');

$ipc = Process::connect();

$server = TcpServer::import($ipc);
$server->setOption(TcpServer::SIMULTANEOUS_ACCEPTS, false);

$id = $ipc->read();

printf("Worker <%s> started\n", $id);

while (true) {
    $socket = $server->accept();
    
    printf("<%s> Accepted connection\n", $id);
    
    (new Timer(300))->awaitTimeout();
    
    $socket->close();
}
