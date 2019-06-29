--TEST--
DNS gethostbynamel using custom resolver.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.dns=1
--FILE--
<?php

namespace Concurrent\DNS;

use Concurrent\TaskScheduler;

TaskScheduler::register(Resolver::class, function () {
    return new class() implements Resolver {
        public function search(Query $query): void {
            var_dump(in_array(Query::A, $query->getTypes()));
            
            $query->addRecord(Query::A, 60, ['ip' => '1.2.3.4']);
        }
    };
});

TaskScheduler::run(function () {
    var_dump(gethostbynamel('foo.bar'));
});

TaskScheduler::register(Resolver::class, function () {
    return new class() implements Resolver {
        public function search(Query $query): void { }
    };
});

TaskScheduler::run(function () {
    var_dump(gethostbynamel('foo.bar'));
});


TaskScheduler::register(Resolver::class, function () {
    return new class() implements Resolver {
        public function search(Query $query): void {
            throw new \Error('FOO!');
        }
    };
});

TaskScheduler::run(function () {
    try {
        gethostbynamel('foo.bar');
    } catch (\Error $e) {
        var_dump($e->getMessage());
    }
});

TaskScheduler::register(Resolver::class, function () {
    return new class() implements Resolver {
        public function search(Query $query): void {
            $query->addRecord(Query::A, 30, []);
        }
    };
});

TaskScheduler::run(function () {
    try {
        gethostbynamel('foo.bar');
    } catch (\Error $e) {
        var_dump($e->getMessage());
    }
});

TaskScheduler::register(Resolver::class, function () {
    return new class() implements Resolver {
        public function search(Query $query): void {
            $query->addRecord(Query::MX, 30, []);
        }
    };
});

TaskScheduler::run(function () {
    try {
        gethostbynamel('foo.bar');
    } catch (\Error $e) {
        var_dump($e->getMessage());
    }
});

--EXPECT--
bool(true)
array(1) {
  [0]=>
  string(7) "1.2.3.4"
}
bool(false)
string(4) "FOO!"
string(37) "DNS A record is missing data key "ip""
string(36) "Unrequested DNS record type: MX (15)"
