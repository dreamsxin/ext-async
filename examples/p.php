<?php

namespace Concurrent\Process;

$reader = function (ReadablePipe $pipe, int $len) {
    try {
        while (null !== ($chunk = $pipe->read($len))) {
            var_dump($chunk);
        }
    } finally {
        $pipe->close();
    }
};

$builder = new ProcessBuilder('ls');
$builder->configureStdout(ProcessBuilder::STDIO_PIPE);
$builder->configureStderr(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDERR);

$proccess = $builder->start(...\array_slice($_SERVER['argv'], 1));

// \Concurrent\Task::async($reader, $proccess->stdout(), 256);

$reader($proccess->stdout(), 8192);

$code = $proccess->awaitExit();

echo "\nEXIT CODE: ", $code, "\n";
