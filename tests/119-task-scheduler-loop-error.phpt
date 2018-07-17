--TEST--
Task scheduler checks state in API methods.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$scheduler = new class() extends LoopTaskScheduler {
    protected function activate() { }
    
    protected function runLoop() {
        static $first = true;
    
        if ($first) {
            $first = false;
            throw new \Error('Fail in loop!');
        }
        
        $this->dispatch();
    }
    
    protected function stopLoop() { }
};

try {
    $scheduler->run(function () { });
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

?>
--EXPECT--
string(13) "Fail in loop!"
