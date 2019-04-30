<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder = $builder->withStdoutInherited();
$builder = $builder->withStderrInherited();

$builder = $builder->withEnv([
    'MY_TITLE' => 'TEST'
], true);

$process = $builder->start(__DIR__ . '/process-p2.php');

\Concurrent\Task::async(function () use ($process) {
    (new \Concurrent\Timer(400))->awaitTimeout();
    var_dump('SIGNAL!');
    $process->signal(\Concurrent\Signal::SIGINT);
});

$code = $process->join();

echo "\nEXIT CODE: ", $code, "\n";

$builder = $builder->withStdinPipe();

$process = $builder->start(__DIR__ . '/process-p1.php');

$stdin = $process->getStdin();

try {
    $stdin->write('Hello');
    
    (new \Concurrent\Timer(100))->awaitTimeout();
    
    $stdin->write(' World ');
    $stdin->write(':)');
} finally {
    $stdin->close();
}

$code = $process->join();

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

$builder = ProcessBuilder::shell();
$builder = $builder->withCwd(__DIR__);

$builder = $builder->withStdoutPipe();
$builder = $builder->withStderrInherited();

$process = $builder->start((\DIRECTORY_SEPARATOR == '\\') ? 'dir' : 'ls');

\Concurrent\Task::async($reader, $process->getStdout(), 256);

$code = $process->join();

echo "\nEXIT CODE: ", $code, "\n";

