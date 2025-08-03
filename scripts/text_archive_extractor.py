#!/usr/bin/env python3
"""
SCR.BIN Text Archive Extractor

Now that we understand SCR.BIN contains text data rather than executable scripts,
this tool properly extracts and catalogs all the game's text content.
"""

import struct
import json
from pathlib import Path
from typing import List, Dict, Any

class TextArchiveExtractor:
    """Extracts text content from the SCR.BIN archive."""
    
    def __init__(self, scr_path: Path):
        self.scr_path = scr_path
        self.file_count = 0
        self.file_info = []
        
    def extract_all_text(self) -> Dict[str, Any]:
        """Extract and analyze all text content from SCR.BIN."""
        print(f"Extracting text from {self.scr_path.name}...")
        
        with open(self.scr_path, 'rb') as f:
            # Read file count
            self.file_count = struct.unpack('<I', f.read(4))[0]
            print(f"Archive contains {self.file_count} text entries")
            
            # Read lookup table (packed offset+size format from previous analysis)
            lookup_table = []
            for i in range(self.file_count):
                entry = struct.unpack('<I', f.read(4))[0]
                # Decode using the successful format from previous analysis
                offset = (entry & 0x1FFFF) << 2  # Lower 17 bits * 4
                size = entry >> 17  # Upper 15 bits
                lookup_table.append((offset, size))
            
            # Extract text entries
            text_entries = []
            for i, (offset, size) in enumerate(lookup_table):
                if offset > 0 and size > 0 and offset < self.scr_path.stat().st_size:
                    f.seek(offset)
                    data = f.read(size)
                    
                    text_entry = self._analyze_text_entry(i, data, offset, size)
                    text_entries.append(text_entry)
        
        # Generate comprehensive text archive analysis
        return {
            'archive_info': {
                'total_entries': self.file_count,
                'extracted_entries': len(text_entries),
                'archive_size': self.scr_path.stat().st_size
            },
            'text_entries': text_entries,
            'text_statistics': self._generate_text_statistics(text_entries),
            'content_categories': self._categorize_content(text_entries)
        }
    
    def _analyze_text_entry(self, entry_id: int, data: bytes, offset: int, size: int) -> Dict[str, Any]:
        """Analyze a single text entry."""
        # Extract readable strings
        strings = self._extract_strings_from_data(data)
        
        # Classify the entry type
        entry_type = self._classify_entry_type(strings, data)
        
        # Calculate text density
        printable_chars = sum(1 for b in data if 32 <= b <= 126)
        text_density = printable_chars / len(data) if len(data) > 0 else 0
        
        return {
            'id': entry_id,
            'offset': offset,
            'size': size,
            'type': entry_type,
            'text_density': text_density,
            'strings': strings,
            'raw_data': data.hex(),
            'has_null_termination': b'\x00' in data,
            'max_string_length': max(len(s['text']) for s in strings) if strings else 0
        }
    
    def _extract_strings_from_data(self, data: bytes) -> List[Dict[str, Any]]:
        """Extract all readable strings from binary data."""
        strings = []
        current_string = b''
        start_pos = 0
        
        for i, byte in enumerate(data):
            if 32 <= byte <= 126:  # Printable ASCII
                if not current_string:
                    start_pos = i
                current_string += bytes([byte])
            else:
                if len(current_string) >= 2:  # Minimum string length lowered for UI text
                    strings.append({
                        'offset': start_pos,
                        'length': len(current_string),
                        'text': current_string.decode('ascii'),
                        'terminated_by': f'0x{byte:02X}' if byte != 0 else 'null'
                    })
                current_string = b''
        
        # Handle string at end
        if len(current_string) >= 2:
            strings.append({
                'offset': start_pos,
                'length': len(current_string),
                'text': current_string.decode('ascii'),
                'terminated_by': 'end_of_entry'
            })
        
        return strings
    
    def _classify_entry_type(self, strings: List[Dict[str, Any]], data: bytes) -> str:
        """Classify what type of text entry this is."""
        if not strings:
            return 'binary_data'
        
        # Combine all text
        all_text = ' '.join(s['text'] for s in strings).lower()
        
        # UI/Menu text patterns
        ui_keywords = ['menu', 'option', 'start', 'select', 'cancel', 'yes', 'no', 
                      'load', 'save', 'game', 'exit', 'back', 'continue']
        
        # Dialog/Story text patterns  
        dialog_keywords = ['said', 'asked', 'replied', 'thought', 'whispered']
        
        # System message patterns
        system_keywords = ['error', 'fail', 'success', 'complete', 'warning', 
                          'memory', 'card', 'data', 'file', 'space']
        
        # Item/Game content patterns
        item_keywords = ['sword', 'armor', 'magic', 'spell', 'potion', 'key', 
                        'treasure', 'monster', 'level', 'hp', 'mp']
        
        # Count keyword matches
        ui_score = sum(1 for keyword in ui_keywords if keyword in all_text)
        dialog_score = sum(1 for keyword in dialog_keywords if keyword in all_text)
        system_score = sum(1 for keyword in system_keywords if keyword in all_text)
        item_score = sum(1 for keyword in item_keywords if keyword in all_text)
        
        # Classify based on highest score
        scores = [
            (ui_score, 'ui_text'),
            (dialog_score, 'dialog_text'),
            (system_score, 'system_message'),
            (item_score, 'game_content')
        ]
        
        max_score = max(scores, key=lambda x: x[0])
        if max_score[0] > 0:
            return max_score[1]
        
        # Fallback classification based on text characteristics
        if len(all_text) > 100:
            return 'long_text'
        elif any(s['text'].isnumeric() for s in strings):
            return 'numeric_data'
        else:
            return 'general_text'
    
    def _generate_text_statistics(self, entries: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Generate statistics about the text archive."""
        total_strings = sum(len(entry['strings']) for entry in entries)
        total_text_length = sum(
            sum(len(s['text']) for s in entry['strings']) 
            for entry in entries
        )
        
        # Entry type distribution
        type_counts = {}
        for entry in entries:
            entry_type = entry['type']
            type_counts[entry_type] = type_counts.get(entry_type, 0) + 1
        
        # Text density distribution
        densities = [entry['text_density'] for entry in entries]
        
        return {
            'total_text_entries': len(entries),
            'total_strings_found': total_strings,
            'total_text_length': total_text_length,
            'average_strings_per_entry': total_strings / len(entries) if entries else 0,
            'entry_type_distribution': type_counts,
            'text_density_stats': {
                'min': min(densities) if densities else 0,
                'max': max(densities) if densities else 0,
                'average': sum(densities) / len(densities) if densities else 0
            }
        }
    
    def _categorize_content(self, entries: List[Dict[str, Any]]) -> Dict[str, List[Dict[str, Any]]]:
        """Categorize content by type for easier browsing."""
        categories = {}
        
        for entry in entries:
            entry_type = entry['type']
            if entry_type not in categories:
                categories[entry_type] = []
            
            # Create summary for this entry
            entry_summary = {
                'id': entry['id'],
                'text_preview': self._get_text_preview(entry['strings']),
                'string_count': len(entry['strings']),
                'size': entry['size']
            }
            
            categories[entry_type].append(entry_summary)
        
        # Sort each category by ID
        for category in categories.values():
            category.sort(key=lambda x: x['id'])
        
        return categories
    
    def _get_text_preview(self, strings: List[Dict[str, Any]]) -> str:
        """Get a preview of the text content."""
        if not strings:
            return '[no text]'
        
        # Combine first few strings
        preview_parts = []
        total_length = 0
        
        for string_info in strings:
            text = string_info['text']
            if total_length + len(text) <= 100:  # Keep preview under 100 chars
                preview_parts.append(text)
                total_length += len(text)
            else:
                # Add partial text and break
                remaining = 100 - total_length
                if remaining > 10:
                    preview_parts.append(text[:remaining] + '...')
                break
        
        return ' | '.join(preview_parts)

def main():
    print("SCR.BIN Text Archive Extractor")
    print("=" * 40)
    
    # Path to SCR.BIN
    disc_path = Path(r"C:\Users\zptha\projects\orphen\orphen_ghidra\Orphen - Scion of Sorcery (USA)\Orphen - Scion of Sorcery (USA)")
    scr_path = disc_path / "SCR.BIN"
    
    if not scr_path.exists():
        print(f"SCR.BIN not found at {scr_path}")
        return 1
    
    # Extract text
    extractor = TextArchiveExtractor(scr_path)
    text_archive = extractor.extract_all_text()
    
    # Save results
    output_file = Path("../text_archive_analysis.json")
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(text_archive, f, indent=2, ensure_ascii=False)
    
    # Print summary
    stats = text_archive['text_statistics']
    print(f"\n=== Text Archive Analysis Complete ===")
    print(f"Total entries: {stats['total_text_entries']}")
    print(f"Total strings found: {stats['total_strings_found']}")
    print(f"Total text length: {stats['total_text_length']} characters")
    print(f"Average text density: {stats['text_density_stats']['average']:.1%}")
    
    print(f"\n=== Content Categories ===")
    categories = text_archive['content_categories']
    for category, entries in categories.items():
        print(f"{category}: {len(entries)} entries")
        # Show a few examples
        for entry in entries[:3]:
            print(f"  #{entry['id']}: {entry['text_preview']}")
        if len(entries) > 3:
            print(f"  ... and {len(entries) - 3} more")
    
    print(f"\nComplete analysis saved to: {output_file}")
    
    return 0

if __name__ == "__main__":
    exit(main())
