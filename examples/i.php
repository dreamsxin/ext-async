<?php

namespace Concurrent\Stream;

new class implements DuplexStream
{
    public function close(?\Throwable $e = null): void { }
    
    public function read(?int $length = null): ?string { }
    
    public function write(string $data): void { }
};
