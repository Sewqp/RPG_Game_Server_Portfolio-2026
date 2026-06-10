const express = require('express');
const logger  = require('./logger');

const app  = express();
const PORT = 3000;

app.use(express.json());

// [라우터 등록]
const authRouter      = require('./routes/auth');
const characterRouter = require('./routes/character');
const auctionRouter   = require('./routes/auction');
const adminRouter     = require('./routes/admin');
const rankingRouter   = require('./routes/ranking');

app.use(authRouter);
app.use(characterRouter);
app.use(auctionRouter);
app.use(adminRouter);
app.use(rankingRouter);

// [전역 에러 핸들러]
app.use((err, req, res, next) => {
    logger.error('전역 에러 발생: ' + err.stack);
    res.status(500).json({ message: '서버 오류가 발생했습니다.' });
});

app.listen(PORT, () => {
    logger.info('서버 시작. Port: ' + PORT);
});

module.exports = { logger };
