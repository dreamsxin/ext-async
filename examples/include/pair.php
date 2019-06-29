<?php

use Concurrent\Deferred;
use Concurrent\Network\TcpServer;
use Concurrent\Network\TcpSocket;
use Concurrent\Network\TlsClientEncryption;
use Concurrent\Network\TlsServerEncryption;
use Concurrent\Task;

function socketpair(): array
{
    $server = TcpServer::listen('127.0.0.1');

    $tasks = [
        Task::async(function () use ($server) {
            try {
                $socket = $server->accept();
            } finally {
                $server->close();
            }

            return $socket;
        }),
        Task::async(function () use ($server) {
            return TcpSocket::connect($server->getAddress(), $server->getPort());
        })
    ];

    $result = array_fill(0, 2, null);

    return Task::await(Deferred::combine($tasks, function (Deferred $defer, bool $last, $k, ?\Throwable $e, $v = null) use (& $result) {
        if ($e) {
            $defer->fail($e);
        } else {
            $result[$k] = $v;

            if ($last) {
                $defer->resolve($result);
            }
        }
    }));
}

function sslpair(): array
{
    $file = dirname(__DIR__) . '/../examples/cert/localhost.';

    $tls = new TlsServerEncryption();
    $tls = $tls->withDefaultCertificate($file . 'crt', $file . 'key', 'localhost');

    $server = TcpServer::listen('localhost', 0, $tls);

    $tasks = [
        Task::async(function () use ($server) {
            try {
                $socket = $server->accept();
            } finally {
                $server->close();
            }

            $socket->encrypt();

            return $socket;
        }),
        Task::async(function () use ($server) {
            $tls = new TlsClientEncryption();
            $tls = $tls->withAllowSelfSigned(true);
            $tls = $tls->withVerifyDepth(5);

            $socket = TcpSocket::connect($server->getAddress(), $server->getPort(), $tls);

            try {
                $socket->encrypt();
            } catch (\Throwable $e) {
                $socket->close();

                throw $e;
            }

            return $socket;
        })
    ];

    $result = array_fill(0, 2, null);

    return Task::await(Deferred::combine($tasks, function (Deferred $defer, bool $last, $k, ?\Throwable $e, $v = null) use (& $result) {
        if ($e) {
            $defer->fail($e);
        } else {
            $result[$k] = $v;

            if ($last) {
                $defer->resolve($result);
            }
        }
    }));
}
