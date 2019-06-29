--TEST--
Task scheduler will shutdown components.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

interface A extends Component { }

TaskScheduler::register(A::class, function () {
    return new class implements A {
        public function shutdown(): void {
            var_dump('SHUTDOWN');
        }
    };
});

TaskScheduler::run(function () {
    TaskScheduler::get(A::class);
});

TaskScheduler::register(A::class, function () {
    return new class implements A {
        public function shutdown(): void {
            throw new \Error('FOO!');
        }
    };
});

try {
    TaskScheduler::run(function () {
        TaskScheduler::get(A::class);
    });
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

TaskScheduler::register(A::class, function () {
    return new class implements A {
        public function shutdown(): void {
            var_dump('B');
            exit;
        }
    };
});

var_dump('A');

TaskScheduler::run(function () {
    TaskScheduler::get(A::class);
});

var_dump('C');

?>
--EXPECT--
string(8) "SHUTDOWN"
string(4) "FOO!"
string(1) "A"
string(1) "B"
