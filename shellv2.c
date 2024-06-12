// version 5.ish du 02 juin 
// plein de problèmes
// plein de fonctions moches
// mais notamment: il faut revoir le pattern matching des redirections (là attrape tout m$eme juste "2")

#include "sys.h"
#include <string.h>
#include <ctype.h>	// pour isspace() 
#include <stdbool.h>
#include <readline/readline.h>

int execute(char *);
int lance_cmd(char *[], bool);
int exec_pipeline(char *[]);
bool premier_plan(char *);
int cmd_internes(char * []);
int moncd(char *[]);
int redirige(char *, char *);
int check_redir(char **);
int decouper(char *, char *, char *[], int);
int est_vide(char *);
int chiffre(char);

enum {
	MaxLigne = 1024, 			// longueur max d'une ligne de commandes
	MaxMot = MaxLigne / 2, 		// nbre max de mot dans la ligne
	MaxDirs = 100, 				// nbre max de repertoire dans PATH
	MaxPathLength = 512, 		// longueur max d'un nom de fichier
	MaxPipes = 128,				// nbre max de commandes séparées par " | " sur une ligne
};

# define PROMPT "V5.3 "
char * path[MaxDirs];			// liste des répertoires de PATH (globale ???)

int main(int argc, char * argv[]){
	char * ligne = NULL;
	int retour;

	/* Découper UNE COPIE de PATH en repertoires */
	decouper(strdup(getenv("PATH")), ":", path, MaxDirs);

	/* Lire et traiter chaque ligne saisie dans le terminal */
	for (; (ligne = readline(PROMPT));){

		if (!est_vide(ligne))
			execute(ligne);

		/* Récupérer les éventuels processus lancés en arrière-plan qui sont terminés */
		while ((retour = waitpid(-1, 0, WNOHANG)) > 0)
			if (retour > 0 ) printf("[Processus %i terminé]\n", retour);
		
		free(ligne);
	}
	printf("Bye :)\n");
	return 0;
}

/* Exécution d'un ligne entière saisie par l'utilisateur */
int execute(char * ligne){
	char * liste_pipes[MaxPipes];	// la ou les (en cas de pipe(s)) commande(s) de la ligne
	char * mot[MaxMot];				// liste des mots de la commande à exécuter
	int tmp;

	bool fg = premier_plan(ligne);
			
	/* si consiste en plusieurs commandes séparées par des pipes */ 
	if (strchr(ligne, '|') != NULL){
		decouper(ligne, "|", liste_pipes, MaxPipes);
		tmp = exec_pipeline(liste_pipes);}	// pid du dernier processus de la pipeline
	
	/* commande unique */
	else {
		decouper(ligne, " \t\n", mot, MaxMot);
		
		/* commande interne --> exécution directement depuis le shell */ 
		if (fg && (cmd_internes(mot) == 0))
			return 0;

		/* sinon, lancement du processus fils */
		if ((tmp = fork()) < 0){
			perror("fork");
			return -1;}

		/* enfant : exécute la commande*/
		if (tmp == 0)
			lance_cmd(mot, fg);
	}

	/* parent : attendre la fin de l'enfant (ou pas) */
	if (!fg) printf("bg : %d\n", tmp);		// bg : on affiche le PID du fils
	else {
		if (waitpid(tmp, 0, 0) < 0){			// fg : on attend la fin du fils
			perror("wait");
			return -1; }
	}
	return 0;
}

/* Exécute une pipeline (une série de commandes séparées par des "|" ) */
int exec_pipeline(char * pipeline[]){
	int i, tmp;
	char * mot[MaxMot];				
	
	/* lancement du sous-shell qui va gérer la pipeline */
	if ((tmp = fork()) < 0){
		perror("fork");
		return -1;}	// !!! bonne réaction ?

	/* [parent] : rien à faire */
	if (tmp > 0) return tmp;

	/* [enfant] : prend en charge la pipeline */
	int pipe_fd[2];				
	int cmd;
	int in = 0;

	for (i = 0; pipeline[i+1] != 0; i++) {	
		if (est_vide(pipeline[i])){
			fprintf(stderr, "Erreur de syntaxe.\n");		// fprintf ou pas nécessaire ?
			exit(-i);}

		/* ouvrir un pipe pour communiquer avec le processus suivant */	
		if (pipe(pipe_fd) != 0) {
			perror("pipe");
			exit(-i);}

		/* créer un enfant pour exécuter la "sous-commande" */
		if ((cmd = fork()) < 0){
			perror("fork");
			exit(-i);}

		if (cmd == 0){				// enfant
			close(pipe_fd[0]); 		// ferme la sortie du tube (inutilisée)
			dup2(pipe_fd[1], 1); 	// redirige sa sortie sur l'entrée du tube
			close(pipe_fd[1]);
			dup2(in, 0);			// redirige la sortie utilisée précédemment sur son entrée
			close(in);
			decouper(pipeline[i], " \t\n", mot, MaxMot);
			lance_cmd(mot, false);	// exécute la commande
		}

		/* Parent */
		close(pipe_fd[1]);			// fermeture de l'entrée du tube actuel
		if (in != 0) close(in);		
		in = pipe_fd[0];
	}

	dup2(in, 0);		// récupère en entrée la sortie du dernier pipe
	close(in); 
	// fprintf(stderr, "Echec de la commande [%d] : \"%s\".\n", -i, pipeline[-i]);

	/* dernière commande de la pipeline */
	decouper(pipeline[i], " \t\n", mot, MaxMot);
	lance_cmd(mot, false);
	return 0;
}

/* exécute une commande, en cherchant l'exécutable dans la liste de répertoires fournie */
int lance_cmd(char * commande[], bool fg){	
	char pathname[MaxPathLength];

	if (cmd_internes(commande) == 0) exit(0);	// c'est une commande interne et elle a été exécutée --> on sort
	
	check_redir(commande);						// mise en place des éventuelles redirections d'E/S

	for (int i = 0; path[i] != 0; i ++){
		snprintf(pathname, sizeof pathname, "%s/%s", path[i], commande[0]);
		execv(pathname, commande);
	}
	fprintf(stderr, "%s: not found\n", commande[0]);
	exit(-1);
}

/* découpe une chaîne de caractères et renvoie le nombre de mots récupérés */
int decouper(char * ligne, char * separ, char * mot[], int maxmot){	
	int i ;
	mot[0] = strtok(ligne, separ);
	for(i = 1; mot[i-1] != 0; i ++){
		if (i == maxmot){
			fprintf (stderr, "Erreur dans la fonction decouper: trop de mots\n");
			mot[i-1] = 0;
			break;
		}
		mot[i] = strtok(NULL, separ);
	}
	return (i-1);
}

/* Renvoie 1 si la chaîne de caractères ne contient que des espaces, 0 sinon */
int est_vide(char *str) {
    while (*str) {
        if (!isspace(*str))
            return 0;
        str++; }
    return 1;
}

// /* Renvoie true ssi la commande doit être exécutée au premier plan */
bool premier_plan(char * ligne){
	char *c = strchr(ligne, '&');
	if (c == NULL || isalnum(*(c+1)))	// pas de "&", ou appartient pas en fin de mot (ex redirection "2>&1")
		return true;
	*c = '\0';							// éliminer le &
	return false;
}

/* si l'entrée correspond à  une commande interne, l'exécuter et renvoyer 0, sinon 1 */
int cmd_internes(char * commande[]){	
	if (strcmp("moncd", commande[0]) == 0){
		moncd(commande);
		return 0;
	}
	// commande de sortie du shell
	else if (strcmp("monexit", commande[0]) == 0)
		exit(0);
	return 1;
}

/* Réalise une redirection basique (<, >, >>, 2>, 2>>, 2>&1, 1>&2) */
int redirige(char * op, char * fichier){	
	int stream;		// descripteur du fichier du / vers lequel on redirige
	int fd;			// flux standard concerné par la redirection (0, 1 ou 2)

	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;  // permissions pour la création de fichier, ici = 644

	if (strcmp(op, "<") == 0) {				// mode lecture
		stream = open(fichier, O_RDONLY);
		fd = 0; }
	else if (strcmp(op, ">") == 0) {		// mode écriture avec création si nécessaire
		stream = open(fichier, O_WRONLY | O_CREAT | O_TRUNC, mode);
		fd = 1; }
	else if (strcmp(op, ">>") == 0){
		stream = open(fichier, O_WRONLY | O_CREAT | O_APPEND, mode);
		fd = 1; }
	else if (strcmp(op, "2>") == 0){
		stream = open(fichier, O_WRONLY | O_CREAT | O_TRUNC, mode);
		fd = 2; }
	else if (strcmp(op, "2>>") == 0){
		stream = open(fichier, O_WRONLY | O_CREAT | O_APPEND, mode);
		fd = 2; }
	else if (op[1] == '>' && op[2] == '&'){		// opérateur de type 2>&1
		fd = chiffre(op[0]);					// flux à rediriger (premier entier de l'opérateur)
		stream = chiffre(op[3]);}				// flux vers lequel rediriger (second entier)
	else {							// si rien de tout ça
		fprintf(stderr, "Erreur : redirection \"%s\" non reconnue.\n", op);
		return -1; }
	
	if (stream < 0) {				// erreur dans l'ouverture du fichier
			perror("open");
			return -1; }
	
	dup2(stream, fd);				// redirection
	if (stream > 2) close(stream);	// fermeture du fichier (hors cas de fusion de flux standards type 2>&1)
	return 0;
}

/* Parcourt un vecteur de mots pour en extraire les redirections */
int check_redir(char * mots[]){
	int i = 0;			// index pour recopier les mots non liés à la redirection
	
	for(int k = 0; mots[k] ; k++) {	
		/* si c'est un potentiel opérateur de redirection (contient les symboles < ou >) */
		if (strchr(mots[k], '<') != NULL || strchr(mots[k], '>') != NULL) {	
			
			/* opérateur à trois symboles max (redirection vers/depuis un fichier) */
			if (strlen(mots[k]) < 4) {		
				if (mots[k+1] == NULL) { 		// dernier mot de la ligne
					fprintf(stderr, "Echec de la redirection : manque le nom du fichier.\n");
					continue; }			
				redirige(mots[k], mots[k+1]);	// faire la redirection
				k++; }							// ignorer le mot suivant (nom du fichier)

			/* opérateur type 2>&1 (redirections entre flux standards) */
			else redirige(mots[k], NULL);		// faire la redirection
		}
		else mots[i++] = mots[k];				// ne recopier que les autres mots
	}
	mots[i]=NULL;								// mettre à jour la fin du vecteur
	return 0; }

/* Convertit un caractère [0-9] en l'entier correspondant */
int chiffre(char c) {return c - '0';}

/* change de répertoire courant */
int moncd(char * commande[]){
	char * dir;
	int len;

	for (len = 0; commande[len] != 0; len ++);	// nombre de mots

	if (len < 2){
		dir = getenv("HOME");
		if (dir == 0) dir = "/tmp";}
	else if (len > 2){
		fprintf(stderr, "usage: %s [dir]\n", commande[0]);
		return 1;}
	else 
		dir = commande[1];
	
	if (chdir(dir) < 0){
		perror(dir);
		return 1;}
	return 0;
}
