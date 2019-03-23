--TEST--
Filesystem can cast stream as fd for process.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--INI--
async.filesystem=1
--FILE--
<?php

namespace Concurrent;

$file = sys_get_temp_dir() . '/' . bin2hex(random_bytes(16)) . '.test';

$spec = [
    [
        'file',
        __DIR__ . '/assets/dir/sub/hello.txt',
        'r'
    ],
    [
        'file',
        $file,
        'w'
    ]
];

$pipes = null;

try {
	$cmd = PHP_BINARY . ' ' . escapeshellarg(__DIR__ . '/assets/upper.php');
    $proc = proc_open($cmd, $spec, $pipes, __DIR__, NULL, [
    	'bypass_shell' => true
    ]);

	var_dump(count($pipes));

    proc_close($proc);
    
    readfile($file);
} finally {
    @unlink($file);
}

--EXPECT--
int(0)
WORLD
