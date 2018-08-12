<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder->configureStdin(ProcessBuilder::STDIO_PIPE);
$builder->configureStdout(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDOUT);
$builder->configureStderr(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDERR);

$process = $builder->start(__DIR__ . '/pp.php');

$stdin = $process->getStdin();

try {
    $stdin->write('Hello');
    
    (new \Concurrent\Timer(100))->awaitTimeout();
    
    $stdin->write(' World ');
    $stdin->write(':)');
} finally {
    $stdin->close();
}

$stdin = null;
$dup = null;

$code = $process->awaitExit();

echo "\nEXIT CODE: ", $code, "\n";

$reader = function (ReadablePipe $pipe, int $len) {
    try {
        while (null !== ($chunk = $pipe->read($len))) {
            var_dump($chunk);
        }
    } finally {
        $pipe->close();
    }
};

$win32 = (\DIRECTORY_SEPARATOR == '\\');

$builder = new ProcessBuilder($win32 ? 'cmd' : 'ls');
$builder->configureStdout(ProcessBuilder::STDIO_PIPE);
$builder->configureStderr(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDERR);

if ($win32) {
    $proccess = $builder->start('/c', 'dir');
} else {
    $proccess = $builder->start(...\array_slice($_SERVER['argv'], 1));
}

// \Concurrent\Task::async($reader, $proccess->getStdout(), 256);

$reader($proccess->getStdout(), 256);

$code = $proccess->awaitExit();

echo "\nEXIT CODE: ", $code, "\n";

