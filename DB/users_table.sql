-- Phase 2: 인증 서버용 users 테이블
USE game_server_schema;

CREATE TABLE IF NOT EXISTS `users` (
    `user_id`       bigint unsigned NOT NULL AUTO_INCREMENT,
    `username`      varchar(30) COLLATE utf8mb4_bin NOT NULL,
    `password_hash` varchar(60) NOT NULL,
    `created_at`    datetime DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`user_id`),
    UNIQUE KEY `uq_username` (`username`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;
