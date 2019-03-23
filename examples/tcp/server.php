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

if (($_SERVER['argv'][1] ?? null)) {
    $tls = new TlsServerEncryption();
    $tls = $tls->withDefaultCertificate(__DIR__ . '/../cert/localhost.crt', __DIR__ . '/../cert/localhost.key', 'localhost');
    $tls = $tls->withAlpnProtocols('foo/bar', 'test/1.0');
} else {
    $tls = null;
}

$server = TcpServer::listen("localhost", 22001, $tls);

try {
    $socket = $server->accept();
    
    try {
        if ($tls) {
            print_r($socket->encrypt());
        }

        $len = 0;

        while (null !== ($chunk = $socket->read())) {
            $len += strlen($chunk);
        }

        var_dump($len);
    } finally {
        $socket->close();
    }
} finally {
    $server->close();
}
