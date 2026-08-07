package com.darpan.host

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import com.darpan.host.databinding.ItemPeerBinding

class PeerAdapter(
    private val onKickPeer: (PeerInfo) -> Unit = {},
) : RecyclerView.Adapter<PeerAdapter.PeerViewHolder>() {

    private var peers: List<PeerInfo> = emptyList()

    fun submit(list: List<PeerInfo>) {
        peers = list
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): PeerViewHolder {
        val binding = ItemPeerBinding.inflate(LayoutInflater.from(parent.context), parent, false)
        return PeerViewHolder(binding, onKickPeer)
    }

    override fun onBindViewHolder(holder: PeerViewHolder, position: Int) {
        holder.bind(peers[position])
    }

    override fun getItemCount(): Int = peers.size

    class PeerViewHolder(
        private val binding: ItemPeerBinding,
        private val onKickPeer: (PeerInfo) -> Unit,
    ) : RecyclerView.ViewHolder(binding.root) {
        fun bind(peer: PeerInfo) {
            binding.peerName.text = peer.displayName.ifBlank { peer.peerId.take(8) }
            binding.peerRole.text = peer.appType.uppercase()
            binding.root.setOnLongClickListener {
                if (peer.appType != "host") {
                    onKickPeer(peer)
                    true
                } else {
                    false
                }
            }
        }
    }
}
