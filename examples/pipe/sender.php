<?php

namespace Concurrent\Network;

error_reporting(-1);

if (DIRECTORY_SEPARATOR == '\\') {
    $file = 'async-test.sock';
} else {
    $file = sprintf('%s/async-test.sock', sys_get_temp_dir());
}

$sock = Pipe::connect($file);

try {
    $chunk = str_repeat('A', 7000);

    for ($i = 0; $i < 1000; $i++) {
        $sock->write($chunk);
    }
} finally {
    $sock->close();
}
