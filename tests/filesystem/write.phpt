--TEST--
Filesystem can write and truncate files.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.filesystem=1
--FILE--
<?php

namespace Concurrent;

$file = sys_get_temp_dir() . '/' . bin2hex(random_bytes(16)) . '.test';

register_shutdown_function(function () use ($file) {
    @unlink($file);
});

$fp = fopen($file, 'wb');

try {
    var_dump(flock($fp, LOCK_EX));
    var_dump(fwrite($fp, "Some more data...\n"));
    var_dump(fwrite($fp, "DONE\n\n"));
    var_dump(fflush($fp));
    var_dump(ftruncate($fp, 12));
} finally {
    fclose($fp);
}

var_dump(file_get_contents($file));

--EXPECT--
bool(true)
int(18)
int(6)
bool(true)
bool(true)
string(12) "Some more da"
