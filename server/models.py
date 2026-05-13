from flask_sqlalchemy import SQLAlchemy
from sqlalchemy.dialects.postgresql import JSONB, ARRAY
from datetime import datetime, timezone

db = SQLAlchemy()

class GameUpload(db.Model):
    __tablename__ = 'game_uploads'
    id          = db.Column(db.Integer, primary_key=True)
    client_id   = db.Column(db.Text, nullable=False, index=True)
    version     = db.Column(db.Integer, nullable=False, default=1)
    uploaded_at = db.Column(db.DateTime(timezone=True), nullable=False,
                            default=lambda: datetime.now(timezone.utc), index=True)
    raw_json    = db.Column(JSONB)
    rounds      = db.relationship('Round', backref='upload', cascade='all, delete-orphan')

class Round(db.Model):
    __tablename__ = 'rounds'
    id                = db.Column(db.Integer, primary_key=True)
    upload_id         = db.Column(db.Integer, db.ForeignKey('game_uploads.id'), nullable=False)
    round_number      = db.Column(db.Integer, nullable=False)
    prevailing_wind   = db.Column(db.SmallInteger, nullable=False)
    dealer_index      = db.Column(db.SmallInteger, nullable=False)
    winner_index      = db.Column(db.SmallInteger)
    self_drawn        = db.Column(db.Boolean, default=False)
    is_draw           = db.Column(db.Boolean, default=False)
    total_faan        = db.Column(db.Integer, default=0)
    is_limit          = db.Column(db.Boolean, default=False)
    scoring_breakdown = db.Column(JSONB)
    final_scores      = db.Column(ARRAY(db.Integer))
    decisions         = db.relationship('Decision', backref='round', cascade='all, delete-orphan')

class Decision(db.Model):
    __tablename__ = 'decisions'
    id             = db.Column(db.Integer, primary_key=True)
    round_id       = db.Column(db.Integer, db.ForeignKey('rounds.id'), nullable=False)
    seq            = db.Column(db.SmallInteger, nullable=False)
    type           = db.Column(db.Text, nullable=False, index=True)
    turn_count     = db.Column(db.Integer, nullable=False)
    wall_remaining = db.Column(db.Integer, nullable=False)
    snapshot       = db.Column(JSONB, nullable=False)
    discarded_tile = db.Column(JSONB)
    claim_type     = db.Column(db.Text)
    claimed_tile   = db.Column(JSONB)
    kong_suit      = db.Column(db.SmallInteger)
    kong_rank      = db.Column(db.SmallInteger)
    winning_tile   = db.Column(JSONB)
