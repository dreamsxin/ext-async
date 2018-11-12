<?php

$file = __DIR__ . '/file-proc-out.txt';

$spec = [
    [
        'pipe',
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
    $proc = proc_open('ls', $spec, $pipes, __DIR__);

    try {
        fclose($pipes[0]);
    } finally {
        proc_close($proc);
    }

    echo "FILE CONTENTS:\n\n";
    readfile($file);
} finally {
    @unlink($file);
}
