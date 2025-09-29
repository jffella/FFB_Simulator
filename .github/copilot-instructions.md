# Instructions pour les agents IA sur FFB_Simulator

Ce projet simule des effets de retour de force pour le volant Microsoft Sidewinder Force Feedback Wheel sous Windows, en utilisant DirectInput. Il est principalement contenu dans `FFB_Simulator.cpp`.

## Architecture et composants
- **Classe principale** : `ForceEffectSimulator` gère l'initialisation DirectInput, la détection du périphérique, la création et le contrôle des effets, et l'interface utilisateur console.
- **Effets supportés** : Effets constants, périodiques (sinus, carré, triangle, dent de scie), rampes, et conditions (ressort, amortissement, inertie, friction).
- **Boucle principale** : Interface utilisateur console avec gestion des touches pour contrôler les effets en temps réel.
- **Thread de mise à jour** : Actualise l'état du périphérique à intervalle régulier.

## Workflows critiques
- **Compilation** :
  - Visual Studio : `cl /EHsc FFB_Simulator.cpp dinput8.lib dxguid.lib`
  - MinGW : `g++ -std=c++11 FFB_Simulator.cpp -ldinput8 -ldxguid -o FFB_Simulator.exe`
- **Dépendances** :
  - Windows SDK ou DirectX SDK
  - Bibliothèques : `dinput8.lib`, `dxguid.lib`
  - Headers : `dinput.h`
- **Exécution** :
  - Nécessite Windows 7+ et le volant Sidewinder connecté
  - Pilotes DirectInput installés

## Conventions et patterns spécifiques
- **Effets** : Les effets sont créés et stockés dans une map `m_Effects` et navigués via `m_EffectNames`.
- **Contrôles utilisateur** :
  - `ESPACE` : Jouer/Arrêter l'effet courant
  - `N/P` : Effet suivant/précédent
  - `S` : Arrêter tous les effets
  - `+/-` : Intensité
  - `←/→` : Direction
  - `↑/↓` : Durée
  - `H` : Aide
  - `ESC` : Quitter
- **Affichage** : Utilisation de `system("cls")` pour rafraîchir la console sous Windows.
- **Gestion mémoire** : Tous les effets sont libérés proprement dans `CleanupEffects()` et lors de l'arrêt.

## Points d'intégration et dépendances externes
- **DirectInput** : Utilisation extensive de l'API DirectInput pour la gestion du périphérique et des effets.
- **Sidewinder VID/PID** : Détection du périphérique via VID/PID (0x045E/0x0034).

## Fichiers clés
- `FFB_Simulator.cpp` : Toute la logique du simulateur.

---

Pour toute modification, respecter la structure de la classe principale et les conventions de gestion des effets. Documenter tout nouveau workflow ou effet ajouté.
