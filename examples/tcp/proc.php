<?php

/*
 +----------------------------------------------------------------------+
 | PHP Version 7                                                        |
 +----------------------------------------------------------------------+
 | Copyright (c) 1997-2018 The PHP Group                                |
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
use Concurrent\Process\ProcessBuilder;

error_reporting(-1);
ini_set('display_errors', '1');

$server = TcpServer::bind("localhost", 10011);
$builder = ProcessBuilder::fork(__DIR__ . '/worker.php');

$procs = [];
$id = 'A';

for ($i = 0; $i < 4; $i++) {
    $procs[] = $process = $builder->start();
    $ipc = $process->getIpc();

    $server->export($ipc);    
    $ipc->writeAsync($id++);
}

$server->close();

$signal = new SignalWatcher(SignalWatcher::SIGINT);
$signal->awaitSignal();

foreach ($procs as $process) {
    if ($process->isRunning()) {
        $process->signal(SignalWatcher::SIGINT);
    }
}

foreach ($procs as $process) {
    $process->join();
}
