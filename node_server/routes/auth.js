const express  = require('express');
const bcrypt   = require('bcrypt');
const jwt      = require('jsonwebtoken');
const pool     = require('../db');
const redis    = require('../redis');
const logger   = require('../logger');
const authMiddleware          = require('../middleware/authMiddleware');
const { SESSION_CACHE_KEY }   = require('../constants');

const router     = express.Router();
const JWT_SECRET = process.env.JWT_SECRET || 'dev-secret-key';
const SESSION_TTL = 3600;

// [회원가입]
router.post('/auth/register', async (req, res, next) => {
    const { username, password } = req.body;
    if (!username || !password) {
        return res.status(400).json({ message: 'username, password 필수입니다.' });
    }

    try {
        const [rows] = await pool.query('SELECT user_id FROM users WHERE username = ?', [username]);
        if (rows.length > 0) {
            return res.status(409).json({ message: '이미 존재하는 username입니다.' });
        }

        const hash = await bcrypt.hash(password, 10);
        await pool.query('INSERT INTO users (username, password_hash) VALUES (?, ?)', [username, hash]);

        logger.info('회원가입 완료: ' + username);
        res.status(201).json({ message: '회원가입 완료.' });
    } catch (err) {
        next(err);
    }
});

// [로그인]
router.post('/auth/login', async (req, res, next) => {
    const { username, password } = req.body;
    if (!username || !password) {
        return res.status(400).json({ message: 'username, password 필수입니다.' });
    }

    try {
        const [rows] = await pool.query('SELECT user_id, password_hash FROM users WHERE username = ?', [username]);
        if (rows.length === 0) {
            return res.status(401).json({ message: '아이디 또는 비밀번호가 틀립니다.' });
        }

        const { user_id, password_hash } = rows[0];
        const match = await bcrypt.compare(password, password_hash);
        if (!match) {
            return res.status(401).json({ message: '아이디 또는 비밀번호가 틀립니다.' });
        }

        const token = jwt.sign({ userId: user_id, username }, JWT_SECRET, { expiresIn: SESSION_TTL });
        await redis.set(SESSION_CACHE_KEY + user_id, token, 'EX', SESSION_TTL);

        logger.info('로그인 성공: ' + username);
        res.json({ token });
    } catch (err) {
        next(err);
    }
});

// [로그아웃]
router.post('/auth/logout', authMiddleware, async (req, res, next) => {
    try {
        await redis.del(SESSION_CACHE_KEY + req.user.userId);
        logger.info('로그아웃: ' + req.user.username);
        res.json({ message: '로그아웃 완료.' });
    } catch (err) {
        next(err);
    }
});

module.exports = router;
