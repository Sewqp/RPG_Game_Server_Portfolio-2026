const jwt    = require('jsonwebtoken');
const redis  = require('../redis');
const { SESSION_CACHE_KEY } = require('../constants');

const JWT_SECRET = process.env.JWT_SECRET || 'dev-secret-key';

async function authMiddleware(req, res, next) {
    const authHeader = req.headers['authorization'];
    if (!authHeader || !authHeader.startsWith('Bearer ')) {
        return res.status(401).json({ message: '인증 토큰이 없습니다.' });
    }

    const token = authHeader.slice(7);

    let payload;
    try {
        payload = jwt.verify(token, JWT_SECRET);
    } catch {
        return res.status(401).json({ message: '유효하지 않은 토큰입니다.' });
    }

    const stored = await redis.get(SESSION_CACHE_KEY + payload.userId);
    if (!stored) {
        return res.status(401).json({ message: '만료된 세션입니다.' });
    }

    req.user = { userId: payload.userId, username: payload.username };
    next();
}

module.exports = authMiddleware;
