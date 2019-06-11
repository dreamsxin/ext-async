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

error_reporting(-1);
ini_set('display_errors', (DIRECTORY_SEPARATOR == '\\') ? '0' : '1');

$async = !empty($_SERVER['argv'][1]);

require_once dirname(__DIR__) . '/include/pair.php';

list ($a, $b) = sslpair();

$chunkSize = 7000;
$count = 100000;

Task::async(function () use ($a, $async, $chunkSize, $count) {
    try {
        $chunk = str_repeat('A', $chunkSize);

        for ($i = 0; $i < $count; $i++) {
            if ($async) {
                Task::async([
                    $a,
                    'write'
                ], $chunk);
            } else {
                $a->write($chunk);
            }
        }
        
        $a->flush();
    } catch (\Throwable $e) {
        echo $e, "\n\n";
    } finally {
        $a->close();
    }
});

$len = 0;
$time = microtime(true);

try {
    while (null !== ($chunk = $b->read())) {
        $len += strlen($chunk);

        if (!preg_match("'^A+$'", $chunk)) {
            throw new \Error('Corrupted data received');
        }
    }
} finally {
    $b->close();
}

if (($chunkSize * $count) != $len) {
    throw new \RuntimeException('Unexpected message size');
}

printf("Time taken: %.2f seconds\n", microtime(true) - $time);
printf("Data transferred: %.2f MB\n", $len / 1024 / 1024);
printf("Memory usage: %.2f MB\n\n", memory_get_peak_usage() / 1024 / 1024);
    