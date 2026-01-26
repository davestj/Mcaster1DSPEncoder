<?php
// traits.db.class.php — Central PDO/MariaDB abstraction trait for Mcaster1.
// Used via `use Mc1Db;` in any class that needs DB access.
// No exit()/die() — uopz active. Foreign key + unique checks disabled on connect.

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);
    return;
}

trait Mc1Db
{
    // db() delegates to the global mc1_db() factory (db.php) so all code
    // shares one PDO connection pool — no duplicate caches.
    protected static function db(string $name = 'mcaster1_media'): PDO
    {
        return mc1_db($name);
    }

    protected static function row(string $db, string $sql, array $p = []): ?array
    {
        $st = self::db($db)->prepare($sql);
        $st->execute($p);
        $r = $st->fetch();
        return $r === false ? null : $r;
    }

    protected static function rows(string $db, string $sql, array $p = []): array
    {
        $st = self::db($db)->prepare($sql);
        $st->execute($p);
        return $st->fetchAll();
    }

    protected static function run(string $db, string $sql, array $p = []): int
    {
        $st = self::db($db)->prepare($sql);
        $st->execute($p);
        return $st->rowCount();
    }

    protected static function lastId(string $db): string
    {
        return self::db($db)->lastInsertId();
    }

    protected static function scalar(string $db, string $sql, array $p = []): mixed
    {
        $st = self::db($db)->prepare($sql);
        $st->execute($p);
        return $st->fetchColumn();
    }
}
