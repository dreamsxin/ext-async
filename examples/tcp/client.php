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

namespace Concurrent\Network;

error_reporting(-1);
ini_set('display_errors', (DIRECTORY_SEPARATOR == '\\') ? '0' : '1');

if ($_SERVER['argv'][1] ?? null) {
    $tls = new TlsClientEncryption();
    $tls = $tls->withAllowSelfSigned(true);
    $tls = $tls->withAlpnProtocols('test/1.1', 'test/1.0');
} else {
    $tls = null;
}

$socket = TcpSocket::connect('localhost', 22001, $tls);

try {
    var_dump('Connection established!');
    
    if ($tls) {
        print_r($socket->encrypt());
    }
    
    $chunk = \str_repeat('A', 6000);

    for ($i = 0; $i < 100000; $i++) {
        $socket->write($chunk);
    }
} finally {
    $socket->close();
}
