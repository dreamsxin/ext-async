--TEST--
Filesystem can cast stream for select.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.filesystem=1
--FILE--
<?php

namespace Concurrent;

$fp = fopen(__FILE__, 'rb');

try {
    $r = [
        $fp
    ];
    $w = [];
    $e = [];

    var_dump(stream_select($r, $w, $e, 0, 1000));
    var_dump(count($r));
} finally {
    fclose($fp);
}

--EXPECT--
int(1)
int(1)
