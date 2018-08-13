<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder->configureStdout(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDOUT);
$builder->configureStderr(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDERR);

$process = $builder->start(__DIR__ . '/ps.php');

\Concurrent\Task::async(function () use ($process) {
    (new \Concurrent\Timer(400))->awaitTimeout();
    var_dump('SIGNAL!');
    $process->signal(\Concurrent\SignalWatcher::SIGINT);
});

$code = $process->awaitExit();

echo "\nEXIT CODE: ", $code, "\n";

$builder->configureStdin(ProcessBuilder::STDIO_PIPE);

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
$builder->setDirectory(__DIR__);
$builder->configureStdout(ProcessBuilder::STDIO_PIPE);
$builder->configureStderr(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDERR);

if ($win32) {
    $process = $builder->start('/c', 'dir');
} else {
    $process = $builder->start(...\array_slice($_SERVER['argv'], 1));
}

// \Concurrent\Task::async($reader, $process->getStdout(), 256);

$reader($process->getStdout(), 256);

$code = $process->awaitExit();

echo "\nEXIT CODE: ", $code, "\n";

