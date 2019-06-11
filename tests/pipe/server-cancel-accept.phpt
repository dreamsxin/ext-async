--TEST--
Pipe server accept can be cancelled.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\CancellationException;
use Concurrent\Context;

if (DIRECTORY_SEPARATOR == '\\') {
    $url = sprintf('\\\\.\\pipe\\%s.sock', bin2hex(random_bytes(16)));
} else {
    $url = sprintf('%s/%s.sock', sys_get_temp_dir(), bin2hex(random_bytes(16)));
    
    if (is_file($url)) {
    	unlink($file);
    }
}

$server = PipeServer::listen($url);
$context = Context::current()->withTimeout(50);

try {
    $context->run(function () use ($server) {
        $server->accept();
    });
} catch (CancellationException $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(21) "Context has timed out"
