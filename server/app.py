#!/usr/bin/env python3
"""
Model weight serving API and game replay upload for Mahjong HK.

Endpoints:
  GET  /api/models/version          -> current model version and checksums
  GET  /api/models/discard          -> download discard_weights.bin
  GET  /api/models/claim            -> download claim_weights.bin
  POST /api/games                   -> upload game replay data
  GET  /api/training/decisions      -> query training decisions
  GET  /health                      -> health check

Usage:
  pip install -r requirements.txt
  python server/app.py [--model-dir assets/model] [--port 8080] [--db-url postgresql://localhost/mahjong]
"""

import os
import sys
import json
import hashlib
import argparse
from pathlib import Path

from flask import Flask, send_file, jsonify, abort, request
from models import db, GameUpload, Round, Decision

app = Flask(__name__)

# Configuration (set via CLI args or environment)
MODEL_DIR = os.environ.get("MODEL_DIR", "assets/model")


def file_sha256(path: str) -> str:
    """Compute SHA-256 hash of a file."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def get_model_version() -> dict:
    """Build version info from the current model files."""
    version_file = os.path.join(MODEL_DIR, "training_stats.json")
    version = 0

    if os.path.exists(version_file):
        try:
            with open(version_file) as f:
                stats = json.load(f)
                version = stats.get("total_games", 0)
        except (json.JSONDecodeError, OSError):
            pass

    discard_path = os.path.join(MODEL_DIR, "discard_weights.bin")
    claim_path = os.path.join(MODEL_DIR, "claim_weights.bin")

    result = {"version": version, "models": {}}

    if os.path.exists(discard_path):
        result["models"]["discard"] = {
            "file": "discard_weights.bin",
            "size": os.path.getsize(discard_path),
            "sha256": file_sha256(discard_path),
        }

    if os.path.exists(claim_path):
        result["models"]["claim"] = {
            "file": "claim_weights.bin",
            "size": os.path.getsize(claim_path),
            "sha256": file_sha256(claim_path),
        }

    return result


@app.route("/api/models/version")
def model_version():
    """Return current model version and checksums."""
    return jsonify(get_model_version())


@app.route("/api/models/discard")
def download_discard():
    """Download discard network weights."""
    path = os.path.join(MODEL_DIR, "discard_weights.bin")
    if not os.path.exists(path):
        abort(404, description="Discard weights not found. Run export_weights.py first.")
    return send_file(path, mimetype="application/octet-stream",
                     as_attachment=True, download_name="discard_weights.bin")


@app.route("/api/models/claim")
def download_claim():
    """Download claim network weights."""
    path = os.path.join(MODEL_DIR, "claim_weights.bin")
    if not os.path.exists(path):
        abort(404, description="Claim weights not found. Run export_weights.py first.")
    return send_file(path, mimetype="application/octet-stream",
                     as_attachment=True, download_name="claim_weights.bin")


@app.route("/api/games", methods=["POST"])
def upload_game():
    """Upload game replay data for training."""
    data = request.get_json(silent=True)
    if not data:
        abort(400, description="Request body must be valid JSON")

    rounds_data = data.get("rounds")
    if not rounds_data or not isinstance(rounds_data, list):
        abort(400, description="JSON must contain a 'rounds' array")

    # Create upload record with raw JSON backup
    upload = GameUpload(
        client_id=data.get("clientId", "unknown"),
        version=data.get("version", 1),
        raw_json=data,
    )
    db.session.add(upload)
    db.session.flush()  # Get upload.id

    total_decisions = 0
    for rd in rounds_data:
        result = rd.get("result", {})
        scoring = result.get("scoring", {})

        round_row = Round(
            upload_id=upload.id,
            round_number=rd.get("roundNumber", 0),
            prevailing_wind=rd.get("prevailingWind", 0),
            dealer_index=rd.get("dealerIndex", 0),
            winner_index=result.get("winnerIndex"),
            self_drawn=result.get("selfDrawn", False),
            is_draw=result.get("isDraw", False),
            total_faan=scoring.get("totalFaan", 0),
            is_limit=scoring.get("isLimit", False),
            scoring_breakdown=scoring.get("breakdown"),
            final_scores=result.get("finalScores"),
        )
        db.session.add(round_row)
        db.session.flush()

        decisions = rd.get("decisions", [])
        for seq, dec in enumerate(decisions):
            decision_row = Decision(
                round_id=round_row.id,
                seq=seq,
                type=dec.get("type", "unknown"),
                turn_count=dec.get("turnCount", 0),
                wall_remaining=dec.get("wallRemaining", 0),
                snapshot=dec.get("snapshot", {}),
                discarded_tile=dec.get("discardedTile"),
                claim_type=dec.get("claimType"),
                claimed_tile=dec.get("claimedTile"),
                kong_suit=dec.get("kongSuit"),
                kong_rank=dec.get("kongRank"),
                winning_tile=dec.get("winningTile"),
            )
            db.session.add(decision_row)
            total_decisions += 1

    db.session.commit()

    return jsonify({
        "status": "ok",
        "uploadId": upload.id,
        "rounds": len(rounds_data),
        "decisions": total_decisions,
    }), 201


@app.route("/api/training/decisions")
def training_decisions():
    """Query training decisions with optional filters."""
    dec_type = request.args.get("type")
    limit = request.args.get("limit", 100, type=int)
    offset = request.args.get("offset", 0, type=int)
    since = request.args.get("since")

    query = Decision.query

    if dec_type:
        query = query.filter_by(type=dec_type)

    if since:
        # Filter by upload timestamp
        from datetime import datetime
        try:
            since_dt = datetime.fromisoformat(since)
            query = query.join(Round).join(GameUpload).filter(
                GameUpload.uploaded_at >= since_dt
            )
        except ValueError:
            pass

    total = query.count()
    decisions = query.order_by(Decision.id).offset(offset).limit(limit).all()

    result = []
    for d in decisions:
        entry = {
            "id": d.id,
            "type": d.type,
            "turnCount": d.turn_count,
            "wallRemaining": d.wall_remaining,
            "snapshot": d.snapshot,
        }
        if d.discarded_tile:
            entry["discardedTile"] = d.discarded_tile
        if d.claim_type:
            entry["claimType"] = d.claim_type
        if d.claimed_tile:
            entry["claimedTile"] = d.claimed_tile
        if d.kong_suit is not None:
            entry["kongSuit"] = d.kong_suit
        if d.kong_rank is not None:
            entry["kongRank"] = d.kong_rank
        if d.winning_tile:
            entry["winningTile"] = d.winning_tile
        result.append(entry)

    return jsonify({
        "decisions": result,
        "total": total,
        "limit": limit,
        "offset": offset,
    })


@app.route("/health")
def health():
    """Health check endpoint."""
    return jsonify({"status": "ok"})


def main():
    global MODEL_DIR

    parser = argparse.ArgumentParser(description="Mahjong HK model weight server")
    parser.add_argument("--model-dir", default=MODEL_DIR,
                        help="Directory containing weight files (default: assets/model)")
    parser.add_argument("--port", type=int, default=8080,
                        help="Port to listen on (default: 8080)")
    parser.add_argument("--host", default="0.0.0.0",
                        help="Host to bind to (default: 0.0.0.0)")
    parser.add_argument("--db-url",
                        default=os.environ.get("DATABASE_URL", "postgresql://localhost/mahjong"),
                        help="Database URL (default: env DATABASE_URL or postgresql://localhost/mahjong)")
    args = parser.parse_args()

    MODEL_DIR = args.model_dir
    app.config['SQLALCHEMY_DATABASE_URI'] = args.db_url

    db.init_app(app)

    if not os.path.isdir(MODEL_DIR):
        print(f"Warning: Model directory not found: {MODEL_DIR}")
        print("The server will start but model downloads will return 404.")
        print(f"Run: python scripts/export_weights.py {MODEL_DIR}")

    with app.app_context():
        db.create_all()

    print(f"Model directory: {MODEL_DIR}")
    print(f"Database: {args.db_url}")
    print(f"Starting server on {args.host}:{args.port}")
    app.run(host=args.host, port=args.port, debug=False)


if __name__ == "__main__":
    main()
