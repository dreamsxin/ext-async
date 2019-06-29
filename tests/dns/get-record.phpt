--TEST--
DNS get record.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.dns=1
--FILE--
<?php

namespace Concurrent\DNS;

use Concurrent\TaskScheduler;

var_dump(dns_get_record(gethostname(), DNS_A)[0]['ip']);

TaskScheduler::register(Resolver::class, function () {
    return new class() implements Resolver {
        public function search(Query $query): void {
            var_dump(count($query->getTypes()));
            var_dump($query->getTypes()[0] == Query::A);
            
            $query->addRecord(Query::A, 60, ['ip' => '1.2.3.4']);
        }
    };
});

TaskScheduler::run(function () {
    var_dump(dns_get_record('localhost', DNS_A)[0]['ip']);
});

TaskScheduler::register(Resolver::class, function () {
    return new class() implements Resolver {
        public function search(Query $query): void { }
    };
});

TaskScheduler::run(function () {
    var_dump(dns_get_record(gethostname(), DNS_A)[0]['ip']);
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
        dns_get_record('localhost', DNS_MX);
    } catch (\Error $e) {
        var_dump($e->getMessage());
    }
});

--EXPECTF--
string(%d) "%s.%s.%s.%s"
int(1)
bool(true)
string(7) "1.2.3.4"
string(%d) "%s.%s.%s.%s"
string(4) "FOO!"
